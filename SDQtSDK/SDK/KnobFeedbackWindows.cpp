#include "KnobFeedback.h"
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <limits>
#include <memory>
#include <vector>


namespace {
struct GdiRuntime {
    ULONG_PTR token = 0;
    Gdiplus::Status status;
    GdiRuntime() {
        Gdiplus::GdiplusStartupInput input;
        status = Gdiplus::GdiplusStartup(&token, &input, nullptr);
    }
    ~GdiRuntime() { if (status == Gdiplus::Ok) Gdiplus::GdiplusShutdown(token); }
};
struct ReleaseStream { void operator()(IStream *stream) const { if (stream) stream->Release(); } };
using Stream = std::unique_ptr<IStream, ReleaseStream>;

Stream memoryStream(const QByteArray &bytes = {}) {
    IStream *raw = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &raw))) return {};
    Stream stream(raw);
    if (!bytes.isEmpty()) {
        ULONG written = 0;
        if (FAILED(stream->Write(bytes.constData(), static_cast<ULONG>(bytes.size()), &written)) ||
            written != static_cast<ULONG>(bytes.size())) return {};
        LARGE_INTEGER origin = {};
        if (FAILED(stream->Seek(origin, STREAM_SEEK_SET, nullptr))) return {};
    }
    return stream;
}

bool pngEncoder(CLSID &encoder) {
    UINT count = 0, size = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &size) != Gdiplus::Ok || !size) return false;
    std::vector<unsigned char> storage(size);
    auto codecs = reinterpret_cast<Gdiplus::ImageCodecInfo *>(storage.data());
    if (Gdiplus::GetImageEncoders(count, size, codecs) != Gdiplus::Ok) return false;
    for (UINT index = 0; index < count; ++index) {
        if (wcscmp(codecs[index].MimeType, L"image/png") == 0) {
            encoder = codecs[index].Clsid;
            return true;
        }
    }
    return false;
}

Gdiplus::Color shade(const FeedbackCommand &command) {
    const BYTE alpha = static_cast<BYTE>(((command.color >> 24) & 255) * command.opacity);
    return Gdiplus::Color(alpha, (command.color >> 16) & 255, (command.color >> 8) & 255, command.color & 255);
}
bool text(Gdiplus::Graphics &graphics, const FeedbackCommand &command, const Gdiplus::RectF &rect) {
    if (command.text.isEmpty()) return true;
    Gdiplus::StringFormat format;
    format.SetAlignment(command.alignment == "right" ? Gdiplus::StringAlignmentFar :
        command.alignment == "left" ? Gdiplus::StringAlignmentNear : Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    format.SetTrimming(command.overflow == "ellipsis" ? Gdiplus::StringTrimmingEllipsisCharacter : Gdiplus::StringTrimmingNone);
    Gdiplus::SolidBrush color(shade(command));
    const auto wide = command.text.toStdWString();
    const Gdiplus::FontFamily family(L"Segoe UI");
    const auto *selected = family.GetLastStatus() == Gdiplus::Ok ? &family : Gdiplus::FontFamily::GenericSansSerif();
    const auto style = command.fontWeight >= 600 ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular;
    Gdiplus::REAL size = static_cast<Gdiplus::REAL>(command.fontSize);
    if (command.overflow == "fit") {
        // Measure with the same font and format used for drawing, including glyph padding.
        for (int attempt = 0; attempt < 3; ++attempt) {
            Gdiplus::Font candidate(selected, size, style, Gdiplus::UnitPixel);
            Gdiplus::RectF bounds;
            if (candidate.GetLastStatus() != Gdiplus::Ok || graphics.MeasureString(wide.c_str(),
                static_cast<INT>(wide.size()), &candidate, Gdiplus::PointF(0, 0), &format, &bounds) != Gdiplus::Ok) return false;
            const auto scale = qMin(1.0f, qMin(rect.Width / qMax(1.0f, bounds.Width),
                rect.Height / qMax(1.0f, bounds.Height)));
            if (scale >= 1.0f) break;
            size *= scale * 0.98f;
        }
    }
    Gdiplus::Font font(selected, size, style, Gdiplus::UnitPixel);
    if (font.GetLastStatus() != Gdiplus::Ok) return false;
    const auto saved = graphics.Save();
    graphics.SetClip(rect, Gdiplus::CombineModeIntersect);
    const auto status = graphics.DrawString(wide.c_str(), static_cast<INT>(wide.size()), &font, rect, &format, &color);
    graphics.Restore(saved);
    return status == Gdiplus::Ok;
}
}

QByteArray RenderNativeFeedback(const FeedbackScene &scene)
{
    static GdiRuntime runtime;
    if (runtime.status != Gdiplus::Ok) return {};
    Gdiplus::Bitmap image(scene.outputSize.width(), scene.outputSize.height(), PixelFormat32bppPARGB);
    if (image.GetLastStatus() != Gdiplus::Ok) return {};
    {
        Gdiplus::Graphics graphics(&image);
        if (graphics.GetLastStatus() != Gdiplus::Ok) return {};
        graphics.SetPageUnit(Gdiplus::UnitPixel);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.Clear(Gdiplus::Color(scene.background));
        graphics.ScaleTransform(static_cast<float>(scene.outputSize.width()) / scene.logicalSize.width(),
                                static_cast<float>(scene.outputSize.height()) / scene.logicalSize.height());
        for (const auto &command : scene.commands) {
            const auto &r = command.rect;
            const Gdiplus::RectF rect(static_cast<float>(r.x()), static_cast<float>(r.y()),
                                     static_cast<float>(r.width()), static_cast<float>(r.height()));
            Gdiplus::SolidBrush color(shade(command));
            if (command.type == FeedbackCommand::Fill) {
                graphics.SetSmoothingMode(Gdiplus::SmoothingModeNone);
                if (graphics.FillRectangle(&color, rect) != Gdiplus::Ok) return {};
            } else if (command.type == FeedbackCommand::Triangle) {
                graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                Gdiplus::PointF points[] = {{rect.X + rect.Width / 2, rect.Y},
                    {rect.X, rect.Y + rect.Height}, {rect.X + rect.Width, rect.Y + rect.Height}};
                if (graphics.FillPolygon(&color, points, 3) != Gdiplus::Ok) return {};
            } else if (command.type == FeedbackCommand::Text) {
                if (!text(graphics, command, rect)) return {};
            } else if (!command.image.isEmpty()) {
                auto source = memoryStream(command.image);
                if (!source) return {};
                Gdiplus::Bitmap bitmap(source.get());
                if (bitmap.GetLastStatus() != Gdiplus::Ok || !bitmap.GetWidth() || !bitmap.GetHeight()) return {};
                const double scale = qMin(r.width() / bitmap.GetWidth(), r.height() / bitmap.GetHeight());
                const float width = static_cast<float>(bitmap.GetWidth() * scale), height = static_cast<float>(bitmap.GetHeight() * scale);
                const Gdiplus::RectF target(rect.X + (rect.Width - width) / 2, rect.Y + (rect.Height - height) / 2, width, height);
                Gdiplus::ColorMatrix matrix = {{{1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 1, 0, 0},
                    {0, 0, 0, static_cast<float>(command.opacity), 0}, {0, 0, 0, 0, 1}}};
                Gdiplus::ImageAttributes attributes;
                if (attributes.SetColorMatrix(&matrix) != Gdiplus::Ok ||
                    graphics.DrawImage(&bitmap, target, 0, 0, static_cast<float>(bitmap.GetWidth()),
                        static_cast<float>(bitmap.GetHeight()), Gdiplus::UnitPixel, &attributes) != Gdiplus::Ok) return {};
            }
        }
    }
    CLSID encoder;
    auto output = memoryStream();
    if (!output || !pngEncoder(encoder) || image.Save(output.get(), &encoder, nullptr) != Gdiplus::Ok) return {};
    STATSTG info = {};
    HGLOBAL handle = nullptr;
    if (FAILED(output->Stat(&info, STATFLAG_NONAME)) || info.cbSize.QuadPart > std::numeric_limits<int>::max() ||
        FAILED(GetHGlobalFromStream(output.get(), &handle))) return {};
    const auto *bytes = static_cast<const char *>(GlobalLock(handle));
    if (!bytes) return {};
    const QByteArray png(bytes, static_cast<int>(info.cbSize.QuadPart));
    GlobalUnlock(handle);
    return png;
}
