/*
 * SPCC.cpp  —  Spectrophotometric Color Calibration
 */

#include "SPCC.h"
#include "photometry/StarDetector.h"
#include "core/Logger.h"

#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>
#include <QFile>
#include <QDataStream>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>
#include <omp.h>

// ─── Constants ────────────────────────────────────────────────────────────────
static constexpr double WL_MIN  = 380.0;
static constexpr double WL_MAX  = 780.0;
static constexpr double WL_STEP = 5.0;
static constexpr int    N_WL    = static_cast<int>((WL_MAX - WL_MIN) / WL_STEP) + 1; // 81

static bool hasLinearWCS(const ImageBuffer::Metadata& m)
{
    return !(m.cd1_1 == 0.0 && m.cd1_2 == 0.0 && m.cd2_1 == 0.0 && m.cd2_2 == 0.0);
}

static bool pixelToRaDecLinear(const ImageBuffer::Metadata& m, double x, double y,
                               double& ra, double& dec)
{
    if (!hasLinearWCS(m)) {
        return false;
    }
    const double dx = x + 1.0 - m.crpix1;
    const double dy = y + 1.0 - m.crpix2;
    ra  = m.ra  + m.cd1_1 * dx + m.cd1_2 * dy;
    dec = m.dec + m.cd2_1 * dx + m.cd2_2 * dy;
    return true;
}

// ─── B-V to Pickles interpolation ────────────────────────────────────────────

const PicklesSpectrum& SPCC::picklesByBV(double bv,
                                          const std::vector<PicklesSpectrum>& lib)
{
    size_t best = 0;
    double best_d = std::numeric_limits<double>::max();
    for (size_t i = 0; i < lib.size(); i++) {
        double d = std::fabs(lib[i].bv - bv);
        if (d < best_d) { best_d = d; best = i; }
    }
    return lib[best];
}

double SPCC::bprpToBV(double bprp) {
    // Approximate linear transform from Gaia EDR3 BP-RP to Johnson B-V
    // Fit from Riello et al. 2021 Table A.2 (main-sequence):
    //   B-V ≈ 0.3930 + 0.4750·(BP-RP) − 0.0548·(BP-RP)²
    return 0.3930 + 0.4750 * bprp - 0.0548 * bprp * bprp;
}

// ─── Synthetic channel flux integration ──────────────────────────────────────

void SPCC::predictRatios(double bv,
                          const std::vector<PicklesSpectrum>& lib,
                          const SpectralResponse& resp,
                          double& out_r, double& out_g, double& out_b)
{
    const PicklesSpectrum& spec = picklesByBV(bv, lib);

    double R = 0.0, G = 0.0, B = 0.0;
    int n = static_cast<int>(std::min({spec.flux.size(), resp.r.size(), resp.g.size(), resp.b.size()}));
    n = std::min(n, N_WL);

    for (int i = 0; i < n; i++) {
        double f = spec.flux[i];
        R += f * resp.r[i];
        G += f * resp.g[i];
        B += f * resp.b[i];
    }

    // Normalise to green channel (avoid divide-by-zero)
    if (G < 1e-12) G = 1e-12;
    out_r = R / G;
    out_g = 1.0;
    out_b = B / G;
}

// ─── Aperture photometry ──────────────────────────────────────────────────────

SPCC::ApertureResult SPCC::aperturePhotometry(const ImageBuffer& buf,
                                               double cx, double cy, double radius)
{
    const int w  = buf.width();
    const int h  = buf.height();
    const int ch = buf.channels();
    const std::vector<float>& data = buf.data();

    double sum[3] = {0.0, 0.0, 0.0};
    double sky[3] = {0.0, 0.0, 0.0};
    int npix = 0, nsky = 0;

    double r2  = radius  * radius;
    double r2o = (radius + 5.0) * (radius + 5.0);   // sky annulus outer
    double r2i = (radius + 2.0) * (radius + 2.0);   // sky annulus inner

    int x0 = std::max(0, static_cast<int>(cx - radius - 6));
    int x1 = std::min(w-1, static_cast<int>(cx + radius + 6));
    int y0 = std::max(0, static_cast<int>(cy - radius - 6));
    int y1 = std::min(h-1, static_cast<int>(cy + radius + 6));

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            double dx = x - cx, dy = y - cy;
            double d2 = dx*dx + dy*dy;
            for (int c = 0; c < std::min(ch, 3); c++) {
                const size_t idx = (static_cast<size_t>(y) * w + x) * ch + c;
                double v = data[idx];
                if (d2 <= r2) { sum[c] += v; }
                else if (d2 >= r2i && d2 <= r2o) { sky[c] += v; }
            }
            if (d2 <= r2) npix++;
            else if (d2 >= r2i && d2 <= r2o) nsky++;
        }
    }

    if (npix == 0) return {0.0, 0.0, 0.0, 0.0};
    if (nsky > 0) {
        int ch3 = std::min(ch, 3);
        for (int c = 0; c < ch3; c++) sky[c] /= nsky;
        for (int c = 0; c < ch3; c++) sum[c] -= sky[c] * npix;
    }

    double snr = (sky[1] > 0) ? sum[1] / (std::sqrt(static_cast<double>(npix)) * sky[1]) : 0.0;
    return { sum[0], sum[1], sum[2], snr };
}

// ─── Least-squares colour matrix solve ───────────────────────────────────────
// Solve 3 independent 1D linear regressions: measured → predicted
// For full 3×3 matrix, solve  A·c = b  where A = [r_m, g_m, b_m] per star.

static bool solveLinearLeastSquares(const std::vector<double>& A_flat, // n×3 row-major
                                    const std::vector<double>& b,       // n×1
                                    double x[3])
{
    int n = static_cast<int>(b.size());
    if (n < 3) return false;

    // Normal equations AtA·x = Atb
    double AtA[3][3] = {}, Atb[3] = {};
    for (int i = 0; i < n; i++) {
        const double* row = &A_flat[i * 3];
        for (int j = 0; j < 3; j++) {
            Atb[j] += row[j] * b[i];
            for (int k = 0; k < 3; k++)
                AtA[j][k] += row[j] * row[k];
        }
    }

    // 3×3 Gaussian elimination
    double m[3][4];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) m[i][j] = AtA[i][j];
        m[i][3] = Atb[i];
    }
    for (int col = 0; col < 3; col++) {
        int pivot = col;
        for (int row = col+1; row < 3; row++)
            if (std::fabs(m[row][col]) > std::fabs(m[pivot][col])) pivot = row;
        for (int k = 0; k <= 3; k++) std::swap(m[col][k], m[pivot][k]);
        if (std::fabs(m[col][col]) < 1e-12) return false;
        for (int row = 0; row < 3; row++) {
            if (row == col) continue;
            double f = m[row][col] / m[col][col];
            for (int k = col; k <= 3; k++) m[row][k] -= f * m[col][k];
        }
    }
    for (int i = 0; i < 3; i++) x[i] = m[i][3] / m[i][i];
    return true;
}

bool SPCC::solveColourMatrix(const std::vector<SPCCStar>& stars,
                              bool fullMatrix, double mat[3][3])
{
    // Identity initialisation
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) mat[i][j] = (i==j) ? 1.0 : 0.0;

    std::vector<SPCCStar> used;
    for (auto& s : stars) if (s.used) used.push_back(s);
    if (used.empty()) return false;

    int n = static_cast<int>(used.size());

    if (!fullMatrix) {
        // Diagonal: solve 3 independent scales r=mat[0][0], g=mat[1][1], b=mat[2][2]
        // mat[0][0] * r_meas = r_pred  →  scale = median(r_pred / r_meas)
        for (int c = 0; c < 3; c++) {
            std::vector<double> ratios(n);
            for (int i = 0; i < n; i++) {
                double meas = (c==0) ? used[i].r_adu : (c==1) ? used[i].g_adu : used[i].b_adu;
                double pred = (c==0) ? used[i].pred_r : (c==1) ? used[i].pred_g : used[i].pred_b;
                ratios[i] = (meas > 1e-12) ? pred / meas : 1.0;
            }
            // Robust: use median
            std::sort(ratios.begin(), ratios.end());
            mat[c][c] = ratios[n / 2];
        }
        return true;
    }

    // Full 3×3 matrix: for each output channel c, solve
    //   [r_meas, g_meas, b_meas] · [mat[c][0], mat[c][1], mat[c][2]]ᵀ = pred_c
    for (int c = 0; c < 3; c++) {
        std::vector<double> A_flat(n * 3);
        std::vector<double> b_vec(n);
        for (int i = 0; i < n; i++) {
            A_flat[i*3+0] = used[i].r_adu;
            A_flat[i*3+1] = used[i].g_adu;
            A_flat[i*3+2] = used[i].b_adu;
            b_vec[i] = (c==0) ? used[i].pred_r : (c==1) ? used[i].pred_g : used[i].pred_b;
        }
        double x[3];
        if (!solveLinearLeastSquares(A_flat, b_vec, x)) return false;
        mat[c][0] = x[0]; mat[c][1] = x[1]; mat[c][2] = x[2];
    }
    return true;
}

// ─── Apply colour matrix ──────────────────────────────────────────────────────

void SPCC::applyColourMatrix(ImageBuffer& buf, const double mat[3][3])
{
    if (buf.channels() < 3) return;
    const int N = buf.width() * buf.height();
    const int ch = buf.channels();
    std::vector<float>& data = buf.data();

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < N; i++) {
        const size_t base = static_cast<size_t>(i) * ch;
        double r = data[base + 0];
        double g = data[base + 1];
        double b = data[base + 2];
        double ro = mat[0][0]*r + mat[0][1]*g + mat[0][2]*b;
        double go = mat[1][0]*r + mat[1][1]*g + mat[1][2]*b;
        double bo = mat[2][0]*r + mat[2][1]*g + mat[2][2]*b;
        data[base + 0] = static_cast<float>(std::max(0.0, ro));
        data[base + 1] = static_cast<float>(std::max(0.0, go));
        data[base + 2] = static_cast<float>(std::max(0.0, bo));
    }
}

// ─── Data loading helpers ─────────────────────────────────────────────────────

bool SPCC::loadPicklesLibrary(const QString& path, std::vector<PicklesSpectrum>& out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;

    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setFloatingPointPrecision(QDataStream::DoublePrecision);

    quint32 count;
    ds >> count;
    out.reserve(count);
    for (quint32 i = 0; i < count; i++) {
        PicklesSpectrum sp;
        ds >> sp.bv;
        quint32 n; ds >> n;
        sp.flux.resize(n);
        for (quint32 j = 0; j < n; j++) ds >> sp.flux[j];
        out.push_back(std::move(sp));
    }
    return ds.status() == QDataStream::Ok;
}

namespace {

constexpr int CH_RED = 1;
constexpr int CH_GREEN = 2;
constexpr int CH_BLUE = 4;
constexpr int CH_ALL = CH_RED | CH_GREEN | CH_BLUE;

struct RepoCurve {
    QString type;
    QString model;
    QString name;
    int channelMask = CH_ALL;
    std::vector<double> wl;
    std::vector<double> values;
};

static double wavelengthScaleFromUnit(const QString& unit) {
    if (unit == "nm") return 1.0;
    if (unit == "micrometer") return 1000.0;
    if (unit == "angstrom") return 0.1;
    if (unit == "m") return 1.0e9;
    return 1.0;
}

static int channelMaskFromString(const QString& ch) {
    const QString c = ch.trimmed().toUpper();
    if (c == "RED") return CH_RED;
    if (c == "GREEN") return CH_GREEN;
    if (c == "BLUE") return CH_BLUE;
    if (c == "RED GREEN" || c == "GREEN RED") return CH_RED | CH_GREEN;
    if (c == "GREEN BLUE" || c == "BLUE GREEN") return CH_GREEN | CH_BLUE;
    if (c == "RED BLUE" || c == "BLUE RED") return CH_RED | CH_BLUE;
    if (c == "ALL" || c == "RED GREEN BLUE" || c == "BLUE GREEN RED") return CH_ALL;
    return CH_ALL;
}

static void parseRepoObjectCurve(const QJsonObject& obj, std::vector<RepoCurve>& curves) {
    const QString type = obj.value("type").toString();
    if (type.isEmpty()) return;

    const QJsonObject wlObj = obj.value("wavelength").toObject();
    const QJsonObject vObj = obj.value("values").toObject();
    const QJsonArray wlArr = wlObj.value("value").toArray();
    const QJsonArray vArr = vObj.value("value").toArray();
    if (wlArr.isEmpty() || wlArr.size() != vArr.size()) return;

    RepoCurve c;
    c.type = type;
    c.model = obj.value("model").toString();
    c.name = obj.value("name").toString();
    c.channelMask = channelMaskFromString(obj.value("channel").toString("ALL"));

    const double wlScale = wavelengthScaleFromUnit(wlObj.value("units").toString("nm"));
    const double range = std::max(1e-12, vObj.value("range").toDouble(1.0));
    c.wl.reserve(wlArr.size());
    c.values.reserve(vArr.size());

    for (int i = 0; i < wlArr.size(); i++) {
        c.wl.push_back(wlArr.at(i).toDouble() * wlScale);
        c.values.push_back(vArr.at(i).toDouble() / range);
    }
    curves.push_back(std::move(c));
}

static std::vector<RepoCurve> scanRepoCurves(const QString& dataPath) {
    std::vector<RepoCurve> out;
    QDir root(dataPath);
    if (!root.exists()) return out;

    const QFileInfoList files = root.entryInfoList(
        QStringList() << "*.json",
        QDir::Files | QDir::NoSymLinks,
        QDir::Name);

    for (const QFileInfo& fi : files) {
        if (fi.fileName().contains("schema", Qt::CaseInsensitive)) continue;
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isArray()) {
            for (const QJsonValue& v : doc.array()) {
                if (v.isObject()) parseRepoObjectCurve(v.toObject(), out);
            }
        } else if (doc.isObject()) {
            parseRepoObjectCurve(doc.object(), out);
        }
    }

    const QFileInfoList dirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& d : dirs) {
        const auto sub = scanRepoCurves(d.absoluteFilePath());
        out.insert(out.end(), sub.begin(), sub.end());
    }
    return out;
}

static double interpLinear(const std::vector<double>& x, const std::vector<double>& y, double xp) {
    if (x.empty() || y.empty() || x.size() != y.size()) return 0.0;
    if (xp < x.front() || xp > x.back()) return 0.0;
    auto it = std::lower_bound(x.begin(), x.end(), xp);
    if (it == x.begin()) return y.front();
    if (it == x.end()) return y.back();
    size_t i1 = static_cast<size_t>(it - x.begin());
    size_t i0 = i1 - 1;
    const double x0 = x[i0], x1 = x[i1];
    const double t = (x1 > x0) ? (xp - x0) / (x1 - x0) : 0.0;
    return y[i0] * (1.0 - t) + y[i1] * t;
}

static std::vector<double> resampleToGrid(const RepoCurve& c) {
    std::vector<double> out(N_WL, 0.0);
    for (int i = 0; i < N_WL; i++) {
        const double wl = WL_MIN + i * WL_STEP;
        out[i] = std::max(0.0, interpLinear(c.wl, c.values, wl));
    }
    return out;
}

static bool tryLoadLegacyResponse(const QString& path, const QString& name, SpectralResponse& out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return false;
    for (const QJsonValue& v : doc.array()) {
        const QJsonObject obj = v.toObject();
        if (obj.value("name").toString() != name) continue;
        out = SpectralResponse();
        out.name = name;
        auto arr = [&](const QString& key, std::vector<double>& vec) {
            for (auto x : obj.value(key).toArray()) vec.push_back(x.toDouble());
        };
        arr("wavelength", out.wavelength);
        arr("r", out.r);
        arr("g", out.g);
        arr("b", out.b);
        return !out.r.empty() && !out.g.empty() && !out.b.empty();
    }
    return false;
}

} // namespace

bool SPCC::loadSpectralResponse(const QString& path, const QString& name,
                                 SpectralResponse& out, const QString& filterName)
{
    const QFileInfo info(path);
    const QString dataPath = info.isDir() ? info.absoluteFilePath() : info.absolutePath();

    // Backward compatibility with the old simplified JSON format.
    if (tryLoadLegacyResponse(dataPath + "/filter_responses.json", name, out)) {
        return true;
    }

    const std::vector<RepoCurve> curves = scanRepoCurves(dataPath);
    if (curves.empty()) return false;

    std::vector<RepoCurve> oscSensor;
    RepoCurve monoSensor;
    bool foundMonoSensor = false;

    for (const auto& c : curves) {
        if (c.type == "OSC_SENSOR" && (c.model == name || c.name == name)) {
            oscSensor.push_back(c);
        } else if (c.type == "MONO_SENSOR" && (c.model == name || c.name == name)) {
            monoSensor = c;
            foundMonoSensor = true;
        }
    }

    if (oscSensor.empty() && !foundMonoSensor) {
        return false;
    }

    out = SpectralResponse();
    out.name = name;
    out.wavelength.resize(N_WL);
    out.r.assign(N_WL, 0.0);
    out.g.assign(N_WL, 0.0);
    out.b.assign(N_WL, 0.0);
    for (int i = 0; i < N_WL; i++) {
        out.wavelength[i] = WL_MIN + i * WL_STEP;
    }

    if (foundMonoSensor) {
        const auto mono = resampleToGrid(monoSensor);
        out.r = mono;
        out.g = mono;
        out.b = mono;
    } else {
        for (const auto& c : oscSensor) {
            const auto sampled = resampleToGrid(c);
            if (c.channelMask & CH_RED) out.r = sampled;
            if (c.channelMask & CH_GREEN) out.g = sampled;
            if (c.channelMask & CH_BLUE) out.b = sampled;
        }
    }

    // Optional filter/LPF transfer function multiplication.
    if (!filterName.isEmpty() && filterName != "No Filter" && filterName != "Luminance") {
        for (const auto& c : curves) {
            if (c.model != filterName && c.name != filterName) continue;
            if (c.type != "OSC_FILTER" && c.type != "MONO_FILTER" && c.type != "OSC_LPF") continue;

            const auto f = resampleToGrid(c);
            if (c.channelMask == CH_ALL) {
                for (int i = 0; i < N_WL; i++) {
                    out.r[i] *= f[i];
                    out.g[i] *= f[i];
                    out.b[i] *= f[i];
                }
            } else {
                if (c.channelMask & CH_RED) {
                    for (int i = 0; i < N_WL; i++) out.r[i] *= f[i];
                }
                if (c.channelMask & CH_GREEN) {
                    for (int i = 0; i < N_WL; i++) out.g[i] *= f[i];
                }
                if (c.channelMask & CH_BLUE) {
                    for (int i = 0; i < N_WL; i++) out.b[i] *= f[i];
                }
            }
            break;
        }
    }

    return !out.r.empty() && !out.g.empty() && !out.b.empty();
}

bool SPCC::loadGaiaCatalogue(const QString& path,
                              std::vector<std::array<double,3>>& radecBV)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setFloatingPointPrecision(QDataStream::DoublePrecision);
    quint32 count; ds >> count;
    radecBV.reserve(count);
    for (quint32 i = 0; i < count; i++) {
        double ra, dec, bprp;
        ds >> ra >> dec >> bprp;
        radecBV.push_back({ra, dec, bprpToBV(bprp)});
    }
    return ds.status() == QDataStream::Ok;
}

QStringList SPCC::availableCameraProfiles(const QString& dataPath) {
    QStringList out;

    QFile f(dataPath + "/filter_responses.json");
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        for (const auto& v : doc.array()) {
            const QString name = v.toObject().value("name").toString();
            if (!name.isEmpty()) out << name;
        }
    }

    const auto curves = scanRepoCurves(dataPath);
    QSet<QString> names;
    for (const auto& c : curves) {
        if (c.type == "OSC_SENSOR" || c.type == "MONO_SENSOR") {
            if (!c.model.isEmpty()) names.insert(c.model);
            else if (!c.name.isEmpty()) names.insert(c.name);
        }
    }
    for (const auto& n : names) out << n;

    out.removeDuplicates();
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b){
        return a.toLower() < b.toLower();
    });
    return out;
}

QStringList SPCC::availableFilterProfiles(const QString& dataPath) {
    QStringList out;
    const auto curves = scanRepoCurves(dataPath);
    QSet<QString> names;
    for (const auto& c : curves) {
        if (c.type == "OSC_FILTER" || c.type == "MONO_FILTER" || c.type == "OSC_LPF") {
            if (!c.model.isEmpty()) names.insert(c.model);
            else if (!c.name.isEmpty()) names.insert(c.name);
        }
    }
    for (const auto& n : names) out << n;
    out.removeDuplicates();
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b){
        return a.toLower() < b.toLower();
    });
    return out;
}

// ─── Main calibrate entry point ───────────────────────────────────────────────

SPCCResult SPCC::calibrate(ImageBuffer& buf, const SPCCParams& p)
{
    SPCCResult res;

    if (!buf.isValid() || buf.channels() < 3) {
        res.errorMsg = "SPCC requires a linear 3-channel (RGB) image.";
        return res;
    }

    // ── 1. Load spectral data ────────────────────────────────────────────
    std::vector<PicklesSpectrum> pickles;
    if (!loadPicklesLibrary(p.dataPath + "/pickles_spectra.bin", pickles)) {
        res.errorMsg = "Could not load Pickles spectral library.";
        return res;
    }

    SpectralResponse resp;
    if (!loadSpectralResponse(p.dataPath,
                               p.cameraProfile, resp, p.filterProfile)) {
        res.errorMsg = QString("Camera profile '%1' not found.").arg(p.cameraProfile);
        return res;
    }

    // ── 2. Detect stars ──────────────────────────────────────────────────
    StarDetector detector;
    auto detected = detector.detect(buf, 0);
    res.starsFound = static_cast<int>(detected.size());

    if (detected.empty()) {
        res.errorMsg = "No stars detected in the image.";
        return res;
    }

    // ── 3. Load catalogue ────────────────────────────────────────────────
    std::vector<std::array<double,3>> catalogue; // {ra, dec, bv}
    // If WCS is available in metadata, use it; otherwise use nearest-neighbour
    // coordinate match assuming catalogue is pre-projected
    bool hasCatalogue = loadGaiaCatalogue(p.dataPath + "/gaia_bv_catalogue.bin",
                                           catalogue);
    if (!hasCatalogue) {
        res.errorMsg = "Could not load Gaia catalogue.";
        return res;
    }

    // ── 4. Cross-match and measure ───────────────────────────────────────
    // For each detected star: find nearest catalogue entry using pixel coords
    // (assuming the catalogue was pre-projected to this image's WCS by plate-solve)
    const auto& meta = buf.metadata();
    bool hasWCS = hasLinearWCS(meta);
    if (!hasWCS) {
        res.errorMsg = "SPCC requires a plate-solved image (valid WCS metadata).";
        return res;
    }

    // Build spatial index for catalogue (simple sequential scan for ≤200 stars)
    std::vector<SPCCStar> stars;
    stars.reserve(std::min((int)detected.size(), p.maxStars));

    int used = 0;
    for (auto& det : detected) {
        if (used >= p.maxStars) break;

        // Aperture photometry
        auto ap = aperturePhotometry(buf, det.x, det.y, p.apertureR);
        if (ap.snr < p.minSNR) continue;
        if (ap.r_adu <= 0 || ap.g_adu <= 0 || ap.b_adu <= 0) continue;

        // Find best catalogue match (closest angular separation if WCS, else skip)
        double best_bv = 0.6; // fallback: G2 solar
        double ra_star, dec_star;
        if (!pixelToRaDecLinear(meta, det.x, det.y, ra_star, dec_star)) {
            continue;
        }
        double best_d2 = 1e12;
        for (auto& cat : catalogue) {
            double dra  = (cat[0] - ra_star)  * std::cos(dec_star * M_PI / 180.0);
            double ddec = (cat[1] - dec_star);
            double d2   = dra*dra + ddec*ddec;
            if (d2 < best_d2) { best_d2 = d2; best_bv = cat[2]; }
        }
        // Accept if within 3 arcsec
        if (best_d2 > (3.0/3600.0) * (3.0/3600.0)) continue;

        double pred_r, pred_g, pred_b;
        predictRatios(best_bv, pickles, resp, pred_r, pred_g, pred_b);

        // Normalise measured to green
        double gn = ap.g_adu;
        SPCCStar s;
        s.xImg   = det.x;  s.yImg   = det.y;
        s.r_adu  = ap.r_adu / gn;
        s.g_adu  = 1.0;
        s.b_adu  = ap.b_adu / gn;
        s.bv     = best_bv;
        s.pred_r = pred_r;
        s.pred_g = pred_g;
        s.pred_b = pred_b;
        s.used   = true;
        stars.push_back(s);
        used++;
    }

    res.stars = stars;
    if (stars.empty()) {
        res.errorMsg = "No usable stars after cross-match.";
        return res;
    }

    // ── 5. Sigma-clip outliers ───────────────────────────────────────────
    // Compute residual for each star; exclude those > 2.5σ
    {
        std::vector<double> resids;
        for (auto& s : stars) {
            double dr = s.r_adu - s.pred_r;
            double db = s.b_adu - s.pred_b;
            resids.push_back(std::sqrt(dr*dr + db*db));
        }
        std::vector<double> sortedRes = resids;
        std::sort(sortedRes.begin(), sortedRes.end());
        double med = sortedRes[sortedRes.size()/2];
        std::vector<double> devs;
        for (auto r : resids) devs.push_back(std::fabs(r - med));
        std::sort(devs.begin(), devs.end());
        double mad = devs[devs.size()/2] / 0.6745;
        double thresh = med + 2.5 * mad;
        for (size_t i = 0; i < stars.size(); i++)
            if (resids[i] > thresh) stars[i].used = false;
    }

    // ── 6. Solve colour matrix ───────────────────────────────────────────
    double mat[3][3];
    if (!solveColourMatrix(stars, p.useFullMatrix, mat)) {
        res.errorMsg = "Colour matrix solve failed.";
        return res;
    }

    // ── 7. Solar G2V reference normalisation ────────────────────────────
    if (p.solarReference) {
        double sr, sg, sb;
        predictRatios(0.65, pickles, resp, sr, sg, sb); // B-V=0.65 ≈ G2V
        // Adjust so that a G2V star would come out neutral
        double norm = sg > 1e-12 ? 1.0 / sg : 1.0;
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) mat[i][j] *= norm;
    }

    // ── 8. Apply to image ────────────────────────────────────────────────
    applyColourMatrix(buf, mat);
    buf.setModified(true);

    // ── 9. Fill result ───────────────────────────────────────────────────
    res.success  = true;
    res.starsUsed = static_cast<int>(std::count_if(stars.begin(), stars.end(),
                                                    [](const SPCCStar& s){ return s.used; }));
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) res.corrMatrix[i][j] = mat[i][j];
    res.scaleR = mat[0][0]; res.scaleG = mat[1][1]; res.scaleB = mat[2][2];

    // RMS residual
    double rms = 0.0; int cnt = 0;
    for (auto& s : stars) if (s.used) {
        double dr = s.r_adu - s.pred_r, db = s.b_adu - s.pred_b;
        rms += dr*dr + db*db; cnt++;
    }
    res.residual = cnt > 0 ? std::sqrt(rms / cnt) : 0.0;

    res.logMsg = QString("SPCC: %1 stars used (%2 found) | "
                         "R=×%3  G=×%4  B=×%5 | RMS residual=%6")
        .arg(res.starsUsed).arg(res.starsFound)
        .arg(res.scaleR, 0, 'f', 4)
        .arg(res.scaleG, 0, 'f', 4)
        .arg(res.scaleB, 0, 'f', 4)
        .arg(res.residual, 0, 'f', 5);

    return res;
}

// ─── Get SPCC data mirrors ────────────────────────────────────────────────────
std::vector<SPCCMirror> SPCC::getDataMirrors()
{
    return {
        { "https://zenodo.org/records/17988559/files", "Primary" },
        { "https://gaia.wheep.co.uk", "UK Mirror" }
    };
}
