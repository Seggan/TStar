#include "PixelMathDialog.h"
#include "MainWindowCallbacks.h"
#include "DialogBase.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QRegularExpression>
#include <QString>
#include <cmath>
#include <algorithm>
#include <stack>
#include <vector>
#include <omp.h>

// ============================================================================
// Pixel evaluation context  (carries current-image channels + extra images)
// ============================================================================
struct PMEvalCtx {
    double r = 0, g = 0, b = 0;  // channels of the primary (target) image
    int targetChan = 0;           // 0=R  1=G  2=B  (for auto-channel image refs)

    struct ImgRef {
        const float* data = nullptr;
        int w = 0, h = 0, nchans = 0;

        float get(int px, int py, int chan) const {
            if (!data || px < 0 || px >= w || py < 0 || py >= h) return 0.0f;
            int c = std::min(chan, nchans - 1);
            return data[(py * w + px) * nchans + c];
        }
    };

    std::vector<ImgRef> imgs;  // I1 = imgs[0], I2 = imgs[1], ...
    int px = 0, py = 0;

    // imgIdx1 is 1-based.  chan: 0=R 1=G 2=B  -1=same as targetChan
    double getImg(int imgIdx1, int chan) const {
        if (imgIdx1 < 1 || imgIdx1 > (int)imgs.size()) return 0.0;
        int c = (chan < 0) ? targetChan : chan;
        return imgs[imgIdx1 - 1].get(px, py, c);
    }
};

// ============================================================================
// Expression Parser AST
// ============================================================================
struct ASTNode {
    enum Type {
        ADD, SUB, MUL, DIV, POW, MOD,
        VAR_R, VAR_G, VAR_B, CONST,
        VAR_IMG,  // cross-image variable: imgIdx (1-based), chanIdx (-1=auto)
        FUNC_MTF, FUNC_GAMMA, FUNC_CLAMP, FUNC_MIN, FUNC_MAX, FUNC_IIF,
        FUNC_ABS, FUNC_SQRT, FUNC_LOG, FUNC_SIN, FUNC_COS,
        FUNC_EXP, FUNC_LN, FUNC_LOG2, FUNC_FLOOR, FUNC_CEIL,
        FUNC_TAN, FUNC_POW, FUNC_SIGN, FUNC_TRUNC,
        FUNC_ASIN, FUNC_ACOS, FUNC_ATAN, FUNC_ATAN2,
        FUNC_SINH, FUNC_COSH, FUNC_TANH,
        // Relational & logical
        CMP_GT, CMP_GE, CMP_LT, CMP_LE, CMP_EQ, CMP_NE,
        LOGIC_AND, LOGIC_OR, LOGIC_NOT
    } type;

    double val     = 0;
    int    imgIdx  = 0;   // VAR_IMG: 1-based image index
    int    chanIdx = -1;  // VAR_IMG: -1=auto (same as targetChan), 0=R, 1=G, 2=B
    std::vector<ASTNode*> children;

    ASTNode(Type t) : type(t) {}
    ASTNode(double v) : type(CONST), val(v) {}
    ~ASTNode() { for (auto c : children) delete c; }

    double eval(const PMEvalCtx& ctx) const {
        switch (type) {
            case VAR_R:   return ctx.r;
            case VAR_G:   return ctx.g;
            case VAR_B:   return ctx.b;
            case CONST:   return val;
            case VAR_IMG: return ctx.getImg(imgIdx, chanIdx);
            case ADD: return children[0]->eval(ctx) + children[1]->eval(ctx);
            case SUB: return children[0]->eval(ctx) - children[1]->eval(ctx);
            case MUL: return children[0]->eval(ctx) * children[1]->eval(ctx);
            case DIV: {
                double d = children[1]->eval(ctx);
                return (d == 0.0) ? 1.0 : children[0]->eval(ctx) / d;
            }
            case MOD: {
                double d = children[1]->eval(ctx);
                return (d == 0.0) ? 0.0 : std::fmod(children[0]->eval(ctx), d);
            }
            case POW: return std::pow(children[0]->eval(ctx), children[1]->eval(ctx));
            case FUNC_ABS:   return std::abs(children[0]->eval(ctx));
            case FUNC_SQRT:  return std::sqrt(std::max(0.0, children[0]->eval(ctx)));
            case FUNC_LOG:   return std::log10(std::max(1e-300, children[0]->eval(ctx)));
            case FUNC_LN:    return std::log(std::max(1e-300, children[0]->eval(ctx)));
            case FUNC_LOG2:  return std::log2(std::max(1e-300, children[0]->eval(ctx)));
            case FUNC_EXP:   return std::exp(children[0]->eval(ctx));
            case FUNC_SIN:   return std::sin(children[0]->eval(ctx));
            case FUNC_COS:   return std::cos(children[0]->eval(ctx));
            case FUNC_TAN:   return std::tan(children[0]->eval(ctx));
            case FUNC_ASIN:  return std::asin(children[0]->eval(ctx));
            case FUNC_ACOS:  return std::acos(children[0]->eval(ctx));
            case FUNC_ATAN:  return std::atan(children[0]->eval(ctx));
            case FUNC_ATAN2: return std::atan2(children[0]->eval(ctx), children[1]->eval(ctx));
            case FUNC_SINH:  return std::sinh(children[0]->eval(ctx));
            case FUNC_COSH:  return std::cosh(children[0]->eval(ctx));
            case FUNC_TANH:  return std::tanh(children[0]->eval(ctx));
            case FUNC_FLOOR: return std::floor(children[0]->eval(ctx));
            case FUNC_CEIL:  return std::ceil(children[0]->eval(ctx));
            case FUNC_TRUNC: return std::trunc(children[0]->eval(ctx));
            case FUNC_SIGN: {
                double x = children[0]->eval(ctx);
                return (x > 0.0) ? 1.0 : ((x < 0.0) ? -1.0 : 0.0);
            }
            case FUNC_MIN:  return std::min(children[0]->eval(ctx), children[1]->eval(ctx));
            case FUNC_MAX:  return std::max(children[0]->eval(ctx), children[1]->eval(ctx));
            case FUNC_POW:  return std::pow(children[0]->eval(ctx), children[1]->eval(ctx));
            case FUNC_MTF: {
                double m = children[0]->eval(ctx);
                double x = children[1]->eval(ctx);
                double den = ((2.0 * m - 1.0) * x) - m;
                return (std::abs(den) < 1e-15) ? x : ((m - 1.0) * x) / den;
            }
            case FUNC_GAMMA: return std::pow(std::max(0.0, children[0]->eval(ctx)), children[1]->eval(ctx));
            case FUNC_CLAMP: return std::clamp(children[0]->eval(ctx), children[1]->eval(ctx), children[2]->eval(ctx));
            case FUNC_IIF:   return (children[0]->eval(ctx) > 0.0) ? children[1]->eval(ctx) : children[2]->eval(ctx);
            // Relational
            case CMP_GT: return children[0]->eval(ctx) > children[1]->eval(ctx) ? 1.0 : 0.0;
            case CMP_GE: return children[0]->eval(ctx) >= children[1]->eval(ctx) ? 1.0 : 0.0;
            case CMP_LT: return children[0]->eval(ctx) < children[1]->eval(ctx) ? 1.0 : 0.0;
            case CMP_LE: return children[0]->eval(ctx) <= children[1]->eval(ctx) ? 1.0 : 0.0;
            case CMP_EQ: return children[0]->eval(ctx) == children[1]->eval(ctx) ? 1.0 : 0.0;
            case CMP_NE: return children[0]->eval(ctx) != children[1]->eval(ctx) ? 1.0 : 0.0;
            // Logical
            case LOGIC_AND: return (children[0]->eval(ctx) != 0.0 && children[1]->eval(ctx) != 0.0) ? 1.0 : 0.0;
            case LOGIC_OR:  return (children[0]->eval(ctx) != 0.0 || children[1]->eval(ctx) != 0.0) ? 1.0 : 0.0;
            case LOGIC_NOT: return (children[0]->eval(ctx) == 0.0) ? 1.0 : 0.0;
            default: return 0;
        }
    }
};

// ============================================================================
// Recursive Descent Parser
// Grammar: parse → logicalOr → logicalAnd → comparison → expression → term → power → factor
// ============================================================================
class PMParser {
public:
    PMParser(const QString& s) : m_str(s), m_pos(0) {}

    ASTNode* parse() {
        return parseLogicalOr();
    }

    QString error() const { return m_error; }

private:
    ASTNode* parseLogicalOr() {
        ASTNode* node = parseLogicalAnd();
        while (m_pos < m_str.length()) {
            skipWS();
            if (matchStr("||")) {
                ASTNode* next = new ASTNode(ASTNode::LOGIC_OR);
                next->children.push_back(node);
                next->children.push_back(parseLogicalAnd());
                node = next;
            } else break;
        }
        return node;
    }

    ASTNode* parseLogicalAnd() {
        ASTNode* node = parseComparison();
        while (m_pos < m_str.length()) {
            skipWS();
            if (matchStr("&&")) {
                ASTNode* next = new ASTNode(ASTNode::LOGIC_AND);
                next->children.push_back(node);
                next->children.push_back(parseComparison());
                node = next;
            } else break;
        }
        return node;
    }

    ASTNode* parseComparison() {
        ASTNode* node = parseExpression();
        while (m_pos < m_str.length()) {
            skipWS();
            if (m_pos >= m_str.length()) break;
            if (matchStr("==")) {
                ASTNode* next = new ASTNode(ASTNode::CMP_EQ);
                next->children.push_back(node);
                next->children.push_back(parseExpression());
                node = next;
            } else if (matchStr("!=")) {
                ASTNode* next = new ASTNode(ASTNode::CMP_NE);
                next->children.push_back(node);
                next->children.push_back(parseExpression());
                node = next;
            } else if (matchStr(">=")) {
                ASTNode* next = new ASTNode(ASTNode::CMP_GE);
                next->children.push_back(node);
                next->children.push_back(parseExpression());
                node = next;
            } else if (matchStr("<=")) {
                ASTNode* next = new ASTNode(ASTNode::CMP_LE);
                next->children.push_back(node);
                next->children.push_back(parseExpression());
                node = next;
            } else if (m_str[m_pos] == '>' && (m_pos+1 >= m_str.length() || m_str[m_pos+1] != '=')) {
                m_pos++;
                ASTNode* next = new ASTNode(ASTNode::CMP_GT);
                next->children.push_back(node);
                next->children.push_back(parseExpression());
                node = next;
            } else if (m_str[m_pos] == '<' && (m_pos+1 >= m_str.length() || m_str[m_pos+1] != '=')) {
                m_pos++;
                ASTNode* next = new ASTNode(ASTNode::CMP_LT);
                next->children.push_back(node);
                next->children.push_back(parseExpression());
                node = next;
            } else break;
        }
        return node;
    }

    ASTNode* parseExpression() {
        ASTNode* node = parseTerm();
        while (m_pos < m_str.length()) {
            skipWS();
            if (m_pos >= m_str.length()) break;
            if (m_str[m_pos] == '+') {
                m_pos++;
                ASTNode* next = new ASTNode(ASTNode::ADD);
                next->children.push_back(node);
                next->children.push_back(parseTerm());
                node = next;
            } else if (m_str[m_pos] == '-') {
                m_pos++;
                ASTNode* next = new ASTNode(ASTNode::SUB);
                next->children.push_back(node);
                next->children.push_back(parseTerm());
                node = next;
            } else break;
        }
        return node;
    }

    ASTNode* parseTerm() {
        ASTNode* node = parsePower();
        while (m_pos < m_str.length()) {
            skipWS();
            if (m_pos >= m_str.length()) break;
            if (m_str[m_pos] == '*') {
                m_pos++;
                ASTNode* next = new ASTNode(ASTNode::MUL);
                next->children.push_back(node);
                next->children.push_back(parsePower());
                node = next;
            } else if (m_str[m_pos] == '/') {
                m_pos++;
                ASTNode* next = new ASTNode(ASTNode::DIV);
                next->children.push_back(node);
                next->children.push_back(parsePower());
                node = next;
            } else if (m_str[m_pos] == '%') {
                m_pos++;
                ASTNode* next = new ASTNode(ASTNode::MOD);
                next->children.push_back(node);
                next->children.push_back(parsePower());
                node = next;
            } else break;
        }
        return node;
    }

    ASTNode* parsePower() {
        ASTNode* node = parseFactor();
        skipWS();
        if (m_pos < m_str.length() && m_str[m_pos] == '^') {
            m_pos++;
            ASTNode* next = new ASTNode(ASTNode::POW);
            next->children.push_back(node);
            next->children.push_back(parseFactor()); // right-associative
            node = next;
        }
        return node;
    }

    ASTNode* parseFactor() {
        skipWS();
        if (m_pos >= m_str.length()) return new ASTNode(0.0);

        if (m_str[m_pos] == '~') {
            m_pos++;
            ASTNode* next = new ASTNode(ASTNode::SUB);
            next->children.push_back(new ASTNode(1.0));
            next->children.push_back(parseFactor());
            return next;
        }

        if (m_str[m_pos] == '!') {
            m_pos++;
            ASTNode* next = new ASTNode(ASTNode::LOGIC_NOT);
            next->children.push_back(parseFactor());
            return next;
        }

        if (m_str[m_pos] == '-') {
            m_pos++;
            ASTNode* next = new ASTNode(ASTNode::SUB);
            next->children.push_back(new ASTNode(0.0));
            next->children.push_back(parseFactor());
            return next;
        }

        if (m_str[m_pos] == '(') {
            m_pos++;
            ASTNode* node = parseLogicalOr();
            skipWS();
            if (m_pos >= m_str.length()) {
                m_error = QCoreApplication::translate("PixelMathDialog", "Missing closing parenthesis");
                return node;
            }
            if (m_str[m_pos] == ')') m_pos++;
            else m_error = QCoreApplication::translate("PixelMathDialog", "Expected ')' but found '%1'").arg(m_str[m_pos]);
            return node;
        }

        if (m_str[m_pos].isDigit() || m_str[m_pos] == '.') {
            int start = m_pos;
            while (m_pos < m_str.length() && (m_str[m_pos].isDigit() || m_str[m_pos] == '.' || m_str[m_pos] == 'e' || m_str[m_pos] == 'E'
                   || ((m_str[m_pos] == '+' || m_str[m_pos] == '-') && m_pos > start && (m_str[m_pos-1] == 'e' || m_str[m_pos-1] == 'E')))) m_pos++;
            return new ASTNode(m_str.mid(start, m_pos - start).toDouble());
        }

        if (m_str[m_pos].isLetter()) {
            int start = m_pos;
            while (m_pos < m_str.length() && (m_str[m_pos].isLetterOrNumber() || m_str[m_pos] == '_')) m_pos++;
            QString name = m_str.mid(start, m_pos - start).toLower();

            // Constants
            if (name == "pi") return new ASTNode(3.14159265358979323846);
            if (name == "e") return new ASTNode(2.71828182845904523536);

            if (name == "r") return new ASTNode(ASTNode::VAR_R);
            if (name == "g") return new ASTNode(ASTNode::VAR_G);
            if (name == "b") return new ASTNode(ASTNode::VAR_B);

            // Cross-image variables: I1..I10 with optional .r/.g/.b channel qualifier
            if (name.length() >= 2 && name[0] == 'i') {
                bool ok = false;
                int imgIdx = name.mid(1).toInt(&ok);
                if (ok && imgIdx >= 1 && imgIdx <= 10) {
                    auto* node = new ASTNode(ASTNode::VAR_IMG);
                    node->imgIdx  = imgIdx;
                    node->chanIdx = -1; // auto: same channel as target

                    // Optional dot-channel: I1.r / I1.g / I1.b
                    skipWS();
                    if (m_pos < m_str.length() && m_str[m_pos] == '.') {
                        int savedPos = m_pos;
                        m_pos++; // consume '.'
                        int cs = m_pos;
                        while (m_pos < m_str.length() && m_str[m_pos].isLetter()) m_pos++;
                        QString chan = m_str.mid(cs, m_pos - cs).toLower();
                        if (chan == "r")      node->chanIdx = 0;
                        else if (chan == "g") node->chanIdx = 1;
                        else if (chan == "b") node->chanIdx = 2;
                        else m_pos = savedPos; // unknown suffix — backtrack
                    }
                    return node;
                }
            }

            // Functions
            skipWS();
            if (m_pos < m_str.length() && m_str[m_pos] == '(') {
                m_pos++;
                std::vector<ASTNode*> args;
                skipWS();
                if (m_pos < m_str.length() && m_str[m_pos] != ')') {
                    args.push_back(parseLogicalOr());
                    skipWS();
                    while (m_pos < m_str.length() && m_str[m_pos] == ',') {
                        m_pos++;
                        skipWS();
                        args.push_back(parseLogicalOr());
                        skipWS();
                    }
                }
                if (m_pos >= m_str.length()) {
                    m_error = QCoreApplication::translate("PixelMathDialog", "Unclosed function call: %1").arg(name);
                    for (auto a : args) delete a;
                    return new ASTNode(0.0);
                }
                m_pos++;  // Skip ')'

                ASTNode* func = nullptr;
                // Two-argument functions
                if (name == "mtf" && args.size() == 2) func = new ASTNode(ASTNode::FUNC_MTF);
                else if (name == "gamma" && args.size() == 2) func = new ASTNode(ASTNode::FUNC_GAMMA);
                else if (name == "pow" && args.size() == 2) func = new ASTNode(ASTNode::FUNC_POW);
                else if (name == "min" && args.size() == 2) func = new ASTNode(ASTNode::FUNC_MIN);
                else if (name == "max" && args.size() == 2) func = new ASTNode(ASTNode::FUNC_MAX);
                else if (name == "atan2" && args.size() == 2) func = new ASTNode(ASTNode::FUNC_ATAN2);
                // Three-argument functions
                else if (name == "clamp" && args.size() == 3) func = new ASTNode(ASTNode::FUNC_CLAMP);
                else if ((name == "iif" || name == "iff") && args.size() == 3) func = new ASTNode(ASTNode::FUNC_IIF);
                // One-argument functions
                else if (name == "abs" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_ABS);
                else if (name == "sqrt" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_SQRT);
                else if (name == "log" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_LOG);
                else if (name == "log10" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_LOG);
                else if (name == "ln" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_LN);
                else if (name == "log2" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_LOG2);
                else if (name == "exp" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_EXP);
                else if (name == "sin" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_SIN);
                else if (name == "cos" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_COS);
                else if (name == "tan" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_TAN);
                else if (name == "asin" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_ASIN);
                else if (name == "acos" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_ACOS);
                else if (name == "atan" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_ATAN);
                else if (name == "sinh" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_SINH);
                else if (name == "cosh" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_COSH);
                else if (name == "tanh" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_TANH);
                else if (name == "floor" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_FLOOR);
                else if (name == "ceil" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_CEIL);
                else if (name == "trunc" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_TRUNC);
                else if (name == "sign" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_SIGN);
                else {
                    for (auto a : args) delete a;
                    if (args.size() > 0) {
                        m_error = QCoreApplication::translate("PixelMathDialog", "Unknown function '%1' with %2 arguments").arg(name).arg(args.size());
                    } else {
                        m_error = QCoreApplication::translate("PixelMathDialog", "Unknown variable or function: '%1'").arg(name);
                    }
                    return new ASTNode(0.0);
                }

                if (func) {
                    func->children = args;
                    return func;
                } else {
                    for (auto a : args) delete a;
                    m_error = QCoreApplication::translate("PixelMathDialog", "Unknown function or wrong arg count: %1").arg(name);
                    return new ASTNode(0.0);
                }
            }

            // Unknown bare identifier
            m_error = QCoreApplication::translate("PixelMathDialog",
                "Unknown variable '%1'. Use r, g, b, or I1..I10 (with optional .r/.g/.b).").arg(name);
        }

        return new ASTNode(0.0);
    }

    bool matchStr(const char* s) {
        int len = (int)strlen(s);
        if (m_pos + len > m_str.length()) return false;
        for (int i = 0; i < len; ++i) {
            if (m_str[m_pos + i].toLatin1() != s[i]) return false;
        }
        m_pos += len;
        return true;
    }

    void skipWS() {
        while (m_pos < m_str.length() && m_str[m_pos].isSpace()) m_pos++;
    }

    QString m_str;
    int m_pos;
    QString m_error;
};

// ============================================================================
// PixelMathDialog Implementation
// ============================================================================

PixelMathDialog::PixelMathDialog(QWidget* parent, ImageViewer* viewer)
    : DialogBase(parent, tr("Pixel Math (Pro)"), 720, 520), m_viewer(viewer)
{
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);
    setupUI();
}

PixelMathDialog::~PixelMathDialog() {}

void PixelMathDialog::setViewer(ImageViewer* viewer) {
    m_viewer = viewer;
}

void PixelMathDialog::setImages(const QVector<PMImageRef>& images) {
    m_images = images;
    updateImageListLabel();
}

void PixelMathDialog::updateImageListLabel() {
    if (m_images.isEmpty()) {
        m_imageListLabel->setText(tr("<i>No additional images loaded.</i>"));
        return;
    }
    QString html = tr("<b>Available images:</b> ");
    QStringList parts;
    for (const auto& img : m_images)
        parts << QString("<b>%1</b> = %2").arg(img.varId, img.name.isEmpty() ? tr("(untitled)") : img.name);
    html += parts.join(tr(" &nbsp;|&nbsp; "));
    m_imageListLabel->setText(html);
}

void PixelMathDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    // Help label
    QLabel* helpLabel = new QLabel(tr(
        "<b>Current image channels:</b> r, g, b &nbsp;&nbsp; <b>Constants:</b> pi, e<br>"
        "<b>Cross-image refs:</b> I1, I2, … I10 &nbsp; (auto-channel) &nbsp; or &nbsp; I1.r / I1.g / I1.b &nbsp; (explicit channel)<br>"
        "<b>Channel targets:</b> R = …; G = …; B = …; &nbsp; (omit any target to leave that channel unchanged)<br>"
        "<b>Single expression</b> (no target): applies to <i>all</i> channels using auto-channel context.<br>"
        "<b>Operators:</b> +  -  *  /  ^  %  ~(invert)  !  &lt;  &lt;=  &gt;  &gt;=  ==  !=  &amp;&amp;  ||<br>"
        "<b>Functions:</b> mtf(m,x)  gamma(x,g)  clamp(x,lo,hi)  iif(cond,t,f)  min  max  pow<br>"
        "&nbsp;&nbsp;&nbsp;abs  sqrt  log  log10  ln  log2  exp  sin  cos  tan  asin  acos  atan  atan2<br>"
        "&nbsp;&nbsp;&nbsp;sinh  cosh  tanh  floor  ceil  trunc  sign<br>"
        "<b>Examples:</b> &nbsp; R = r + I2.r * 0.5; &nbsp;|&nbsp; R = I1.r; &nbsp;|&nbsp; r + I2 * 0.3")
    );
    helpLabel->setWordWrap(true);
    mainLayout->addWidget(helpLabel);

    // Image list label
    QGroupBox* imgGroup = new QGroupBox(tr("Image References"), this);
    QVBoxLayout* imgLayout = new QVBoxLayout(imgGroup);
    m_imageListLabel = new QLabel(tr("<i>No additional images loaded.</i>"), this);
    m_imageListLabel->setWordWrap(true);
    m_imageListLabel->setTextFormat(Qt::RichText);
    imgLayout->addWidget(m_imageListLabel);
    mainLayout->addWidget(imgGroup);

    // Expression editor
    QGroupBox* exprGroup = new QGroupBox(tr("Channel Expressions"), this);
    QVBoxLayout* exprLayout = new QVBoxLayout(exprGroup);
    m_exprEdit = new QPlainTextEdit(this);
    m_exprEdit->setPlaceholderText(tr("R = r + I2.r * 0.5;   (or just)   r + I2 * 0.3"));
    m_exprEdit->setFont(QFont("Consolas", 11));
    m_exprEdit->setMinimumHeight(80);
    exprLayout->addWidget(m_exprEdit);
    mainLayout->addWidget(exprGroup);

    // Options
    m_checkRescale = new QCheckBox(tr("Rescale result (maps min–max to 0–1)"), this);
    mainLayout->addWidget(m_checkRescale);

    // Status
    m_statusLabel = new QLabel(tr("Ready"), this);
    mainLayout->addWidget(m_statusLabel);

    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_btnApply  = new QPushButton(tr("Apply"), this);
    m_btnCancel = new QPushButton(tr("Cancel"), this);

    connect(m_btnApply,  &QPushButton::clicked, this, &PixelMathDialog::onApply);
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    btnLayout->addStretch();
    btnLayout->addWidget(m_btnCancel);
    btnLayout->addWidget(m_btnApply);
    mainLayout->addLayout(btnLayout);
}

// ============================================================================
// Backward-compatible overload (no cross-image references)
// ============================================================================
bool PixelMathDialog::evaluateExpression(const QString& expr, ImageBuffer& buf,
                                          bool rescale, QString* errorMsg) {
    return evaluateExpression(expr, buf, {}, rescale, errorMsg);
}

// ============================================================================
// Main evaluation — supports partial channel assignment + cross-image refs
// ============================================================================
bool PixelMathDialog::evaluateExpression(const QString& expr, ImageBuffer& buf,
                                          const QVector<PMImageRef>& images,
                                          bool rescale, QString* errorMsg) {
    if (!buf.isValid()) {
        if (errorMsg) *errorMsg = tr("No valid image buffer.");
        return false;
    }

    int w = buf.width();
    int h = buf.height();
    int nchans = buf.channels();
    size_t totalPixels = (size_t)w * h;
    std::vector<float>& data = buf.data();
    std::vector<float> src = data; // original snapshot for read-back

    // Build image references for PMEvalCtx (I1=images[0], I2=images[1], ...)
    std::vector<PMEvalCtx::ImgRef> imgRefs;
    imgRefs.reserve(images.size());
    for (const auto& img : images) {
        PMEvalCtx::ImgRef ref;
        if (img.buffer && img.buffer->isValid()) {
            ref.data   = img.buffer->data().data();
            ref.w      = img.buffer->width();
            ref.h      = img.buffer->height();
            ref.nchans = img.buffer->channels();
        }
        imgRefs.push_back(ref);
    }

    // --- Parse channel assignments: R = ...; G = ...; B = ...; ---
    ASTNode* asts[3]        = { nullptr, nullptr, nullptr };
    bool     hasAssign[3]   = { false, false, false };
    ASTNode* sharedAst      = nullptr; // used when single expression applied to all channels

    QRegularExpression reAssign(R"(([RGB])\s*=\s*([^;]+);?)", QRegularExpression::CaseInsensitiveOption);
    auto matches = reAssign.globalMatch(expr);

    while (matches.hasNext()) {
        auto m = matches.next();
        char target   = m.captured(1).toUpper().at(0).toLatin1();
        QString part  = m.captured(2).trimmed();

        PMParser p(part);
        ASTNode* node = p.parse();
        if (!p.error().isEmpty()) {
            if (errorMsg) *errorMsg = tr("Parse error in %1 expression: %2").arg(QChar(target), p.error());
            for (int i = 0; i < 3; ++i) delete asts[i];
            delete node;
            return false;
        }

        int idx = (target == 'R') ? 0 : (target == 'G' ? 1 : 2);
        delete asts[idx];
        asts[idx]      = node;
        hasAssign[idx] = true;
    }

    // If nothing matched, treat the whole expression as a single formula that
    // applies to all three channels (auto-channel context selects the right pixel).
    if (!hasAssign[0] && !hasAssign[1] && !hasAssign[2]) {
        PMParser p(expr.trimmed());
        sharedAst = p.parse();
        if (!p.error().isEmpty() || !sharedAst) {
            if (errorMsg) *errorMsg = tr("Parse error: %1").arg(p.error());
            delete sharedAst;
            return false;
        }
        for (int i = 0; i < 3; ++i) { asts[i] = sharedAst; hasAssign[i] = true; }
    }

    // For mono images only apply channel 0, regardless of what was specified
    int maxChan = std::min(nchans, 3);

    double globalMin =  1e30;
    double globalMax = -1e30;

    #pragma omp parallel
    {
        double localMin =  1e30;
        double localMax = -1e30;

        #pragma omp for schedule(static)
        for (long long pi = 0; pi < (long long)totalPixels; ++pi) {
            int px = (int)(pi % w);
            int py = (int)(pi / w);
            size_t base = pi * nchans;

            // Build eval context for this pixel
            PMEvalCtx ctx;
            ctx.r   = (nchans > 0) ? (double)src[base]     : 0.0;
            ctx.g   = (nchans > 1) ? (double)src[base + 1] : 0.0;
            ctx.b   = (nchans > 2) ? (double)src[base + 2] : 0.0;
            ctx.imgs = imgRefs;
            ctx.px  = px;
            ctx.py  = py;

            for (int c = 0; c < maxChan; ++c) {
                if (!hasAssign[c]) {
                    // Channel not assigned — keep original value
                    data[base + c] = src[base + c];
                    continue;
                }
                ctx.targetChan = c;
                double res = asts[c]->eval(ctx);

                if (rescale) {
                    if (res < localMin) localMin = res;
                    if (res > localMax) localMax = res;
                    data[base + c] = (float)res;
                } else {
                    data[base + c] = (float)std::clamp(res, 0.0, 1.0);
                }
            }
        }

        if (rescale) {
            #pragma omp critical
            {
                if (localMin < globalMin) globalMin = localMin;
                if (localMax > globalMax) globalMax = localMax;
            }
        }
    }

    // AST cleanup — be careful not to double-delete the shared node
    if (sharedAst) {
        delete sharedAst;
    } else {
        for (int i = 0; i < 3; ++i) delete asts[i];
    }

    // Rescale pass (global, affects only the assigned channels' data)
    if (rescale && globalMax > globalMin) {
        double den = globalMax - globalMin;
        #pragma omp parallel for schedule(static)
        for (long long i = 0; i < (long long)data.size(); ++i)
            data[i] = (float)(((double)data[i] - globalMin) / den);
    }

    return true;
}

void PixelMathDialog::onApply() {
    QString expr = m_exprEdit->toPlainText().trimmed();
    if (expr.isEmpty()) {
        QMessageBox::warning(this, tr("Empty Expression"), tr("Please enter an expression."));
        return;
    }
    emit apply(expr, m_checkRescale->isChecked());
}
