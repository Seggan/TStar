// ============================================================================
// CatalogGradientExtractor.cpp
// Reference-based background gradient extraction using polynomial fitting
// on masked sky regions identified from a survey reference image.
// ============================================================================

#include "CatalogGradientExtractor.h"

#include <opencv2/opencv.hpp>
#include <vector>
#include <cmath>
#include <cstring>

namespace Background {

// ============================================================================
// Internal Helpers
// ============================================================================

/**
 * @brief Fit a 2D polynomial to valid sky pixels for robust gradient modeling.
 *
 * Downsamples the source for speed, collects sky-only pixels using the provided
 * mask, rejects outliers via z-score filtering, and solves the polynomial
 * coefficients using SVD decomposition.
 *
 * @param src    Single-channel float source image.
 * @param mask   Binary mask (255 = sky, 0 = nebula/star).
 * @param degree Polynomial degree [1..3].
 * @return Full-resolution polynomial background surface.
 */
static cv::Mat fitPolynomialBackground(const cv::Mat& src,
                                       const cv::Mat& mask,
                                       int degree)
{
    // Downsample for speed and robustness
    constexpr int kMaxSize = 128;
    const double scale = std::min(1.0,
        static_cast<double>(kMaxSize) / std::max(src.cols, src.rows));

    cv::Mat smallSrc, smallMask;
    cv::resize(src,  smallSrc,  cv::Size(), scale, scale, cv::INTER_AREA);
    cv::resize(mask, smallMask, cv::Size(), scale, scale, cv::INTER_NEAREST);

    // -- Collect sky-only sample points ---------------------------------------
    std::vector<cv::Point> pts;
    std::vector<float>     vals;
    pts.reserve(smallSrc.rows * smallSrc.cols);
    vals.reserve(smallSrc.rows * smallSrc.cols);

    for (int y = 0; y < smallSrc.rows; ++y) {
        for (int x = 0; x < smallSrc.cols; ++x) {
            if (smallMask.at<uchar>(y, x) > 128) {
                pts.push_back(cv::Point(x, y));
                vals.push_back(smallSrc.at<float>(y, x));
            }
        }
    }

    // Minimum sample count for the requested degree
    const size_t minPts = (degree == 1) ? 3
                        : (degree == 2) ? 6
                        : (degree == 3) ? 10 : 1;

    if (pts.size() < minPts) {
        cv::Scalar m = cv::mean(src, mask);
        return cv::Mat(src.size(), src.type(), m[0]);
    }

    // -- Outlier rejection using 2-sigma z-score filtering --------------------
    cv::Mat vMat(vals.size(), 1, CV_32F, vals.data());
    cv::Scalar meanV, stdDevV;
    cv::meanStdDev(vMat, meanV, stdDevV);

    std::vector<cv::Point> cleanPts;
    std::vector<float>     cleanVals;
    const float threshold = 2.0f * static_cast<float>(stdDevV[0]);

    for (size_t i = 0; i < vals.size(); ++i) {
        if (std::abs(vals[i] - static_cast<float>(meanV[0])) < threshold) {
            cleanPts.push_back(pts[i]);
            cleanVals.push_back(vals[i]);
        }
    }

    // Fall back to unfiltered if too few inliers
    if (cleanPts.size() < minPts) {
        cleanPts  = pts;
        cleanVals = vals;
    }

    // -- Build design matrix and solve ----------------------------------------
    const int cols = (degree == 1) ? 3
                   : (degree == 2) ? 6
                   : (degree == 3) ? 10 : 15;

    cv::Mat A(cleanPts.size(), cols, CV_32FC1);
    cv::Mat B(cleanPts.size(), 1,    CV_32FC1);

    const float normX = 2.0f / (smallSrc.cols - 1);
    const float normY = 2.0f / (smallSrc.rows - 1);

    for (size_t i = 0; i < cleanPts.size(); ++i) {
        const float x = cleanPts[i].x * normX - 1.0f;
        const float y = cleanPts[i].y * normY - 1.0f;
        B.at<float>(i, 0) = cleanVals[i];

        float* row = A.ptr<float>(i);
        int c = 0;
        row[c++] = 1.0f;
        if (degree >= 1) { row[c++] = x;     row[c++] = y;                       }
        if (degree >= 2) { row[c++] = x * x; row[c++] = y * y; row[c++] = x * y; }
        if (degree >= 3) { row[c++] = x*x*x; row[c++] = y*y*y;
                           row[c++] = x*x*y; row[c++] = x*y*y;                   }
    }

    cv::Mat coeffs;
    cv::solve(A, B, coeffs, cv::DECOMP_SVD);

    // -- Reconstruct full-resolution surface ----------------------------------
    cv::Mat result(src.rows, src.cols, CV_32FC1);
    const float fullNormX  = 2.0f / (src.cols - 1);
    const float fullNormY  = 2.0f / (src.rows - 1);
    const float* cPtr      = coeffs.ptr<float>(0);

    for (int y = 0; y < src.rows; ++y) {
        const float py  = y * fullNormY - 1.0f;
        const float py2 = py * py;
        const float py3 = py2 * py;
        float* row      = result.ptr<float>(y);

        for (int x = 0; x < src.cols; ++x) {
            const float px  = x * fullNormX - 1.0f;
            const float px2 = px * px;
            const float px3 = px2 * px;

            float val = cPtr[0];
            int c = 1;
            if (degree >= 1) { val += cPtr[c]*px + cPtr[c+1]*py;                                c += 2; }
            if (degree >= 2) { val += cPtr[c]*px2 + cPtr[c+1]*py2 + cPtr[c+2]*px*py;            c += 3; }
            if (degree >= 3) { val += cPtr[c]*px3 + cPtr[c+1]*py3 + cPtr[c+2]*px2*py + cPtr[c+3]*px*py2; }

            row[x] = val;
        }
    }

    return result;
}

/**
 * @brief Extract a single channel from a multi-channel cv::Mat.
 * @param mat Source matrix.
 * @param ch  Channel index.
 * @return Single-channel CV_32FC1 matrix.
 */
static cv::Mat extractChannel(const cv::Mat& mat, int ch)
{
    if (mat.channels() == 1) {
        return mat;
    }
    cv::Mat out;
    cv::extractChannel(mat, out, ch);
    return out;
}

// ============================================================================
// Public Interface
// ============================================================================

bool CatalogGradientExtractor::extract(ImageBuffer& target,
                                       const ImageBuffer& reference,
                                       const Options& opts,
                                       std::function<void(int)> progress,
                                       std::atomic<bool>* cancelFlag)
{
    if (progress) progress(5);

    ImageBuffer gradMap = computeGradientMap(target, reference, opts, cancelFlag);
    if (!gradMap.isValid()) return false;
    if (cancelFlag && cancelFlag->load()) return false;

    if (progress) progress(90);

    // If requested, output the raw gradient map instead of the corrected image
    if (opts.outputGradientMap) {
        target = gradMap;
        return true;
    }

    // Subtract the gradient map from the target.
    // No upper clamp to [0,1]: preserves shadows and relative channel brightness.
    // Only clamp at 0 to prevent physically meaningless negative flux values.
    auto&       tgtData = target.data();
    const auto& gd      = gradMap.data();
    const size_t n      = tgtData.size();

    for (size_t i = 0; i < n; ++i) {
        tgtData[i] = tgtData[i] - gd[i];
    }

    if (progress) progress(100);
    return true;
}

ImageBuffer CatalogGradientExtractor::computeGradientMap(
    const ImageBuffer& target,
    const ImageBuffer& reference,
    const Options& /*opts*/,
    std::atomic<bool>* cancelFlag)
{
    ImageBuffer result;

    const int W   = target.width();
    const int H   = target.height();
    const int tCh = target.channels();

    // -- Wrap target data as cv::Mat (zero-copy) ------------------------------
    const int cvTgtType = (tCh == 3) ? CV_32FC3 : CV_32FC1;
    cv::Mat tgtMat(H, W, cvTgtType,
                   const_cast<float*>(target.data().data()));

    // -- Prepare reference cv::Mat --------------------------------------------
    cv::Mat refMat;
    if (reference.isValid()) {
        const int rcvType = (reference.channels() == 3) ? CV_32FC3 : CV_32FC1;
        cv::Mat rawRef(reference.height(), reference.width(), rcvType,
                       const_cast<float*>(reference.data().data()));

        if (rawRef.cols != W || rawRef.rows != H) {
            cv::resize(rawRef, refMat, cv::Size(W, H), 0, 0, cv::INTER_CUBIC);
        } else {
            refMat = rawRef;
        }
    }

    // -- Per-channel gradient extraction ---------------------------------------
    std::vector<cv::Mat> gradChannels;
    gradChannels.reserve(tCh);

    for (int c = 0; c < tCh; ++c) {
        if (cancelFlag && cancelFlag->load()) return {};

        cv::Mat tgtChan = extractChannel(tgtMat, c);
        cv::Mat gradient;

        if (!refMat.empty()) {
            const int refC   = (refMat.channels() == 1) ? 0 : c;
            cv::Mat   refChan = extractChannel(refMat, refC);

            // Step 1: Build sky mask from the reference image
            cv::Mat rBlurred;
            cv::GaussianBlur(refChan, rBlurred, cv::Size(0, 0), 4.0);

            // Compute reference median and MAD for robust sky level estimation
            cv::Mat r1D;
            rBlurred.reshape(1, 1).copyTo(r1D);
            cv::sort(r1D, r1D, cv::SORT_EVERY_ROW + cv::SORT_ASCENDING);
            const float rMedian = r1D.at<float>(0, r1D.cols / 2);

            cv::Mat rDev = cv::abs(r1D - rMedian);
            cv::sort(rDev, rDev, cv::SORT_EVERY_ROW + cv::SORT_ASCENDING);
            const float rMAD   = rDev.at<float>(0, rDev.cols / 2);
            const float rSigma = rMAD * 1.4826f;

            // Binary sky mask: 255 = sky, 0 = nebula/star
            cv::Mat skyMask;
            cv::threshold(rBlurred, skyMask,
                          rMedian + 1.0f * rSigma, 255.0, cv::THRESH_BINARY_INV);
            skyMask.convertTo(skyMask, CV_8U);

            // Erode to grow the protection zone around bright objects
            cv::Mat kernel = cv::getStructuringElement(
                cv::MORPH_ELLIPSE, cv::Size(15, 15));
            cv::erode(skyMask, skyMask, kernel);

            // Step 2: Fit polynomial to target's sky pixels only
            gradient = fitPolynomialBackground(tgtChan, skyMask, 2);

            // Step 3: Zero-center the gradient at sky pixels so subtraction
            //         preserves the original sky median level
            cv::Scalar gSkyMean = cv::mean(gradient, skyMask);
            gradient = gradient - gSkyMean[0];

        } else {
            // Fallback: no reference available, use entire image as sky
            gradient = fitPolynomialBackground(
                tgtChan, cv::Mat::ones(tgtChan.size(), CV_8U) * 255, 2);
            cv::Scalar gMedian = cv::mean(gradient);
            gradient = gradient - gMedian[0];
        }

        gradChannels.push_back(gradient);
    }

    // -- Merge channels and copy into result ImageBuffer -----------------------
    cv::Mat merged;
    if (tCh == 3) {
        cv::merge(gradChannels, merged);
    } else {
        merged = gradChannels[0];
    }

    result.resize(W, H, tCh);
    std::memcpy(result.data().data(), merged.ptr<float>(),
                static_cast<size_t>(W) * H * tCh * sizeof(float));

    return result;
}

} // namespace Background