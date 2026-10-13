// Development-only verification, never part of the release package.
#include <FluentQt/FluentQt.h>
#include "ui/qt/SettingsQtWindow.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <QTimer>
#include <QTextLayout>
#include <QGlyphRun>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <windows.h>

namespace {
using Window = fluent::windowing::Window;
using Toggle = fluent::basicinput::ToggleSwitch;
using Combo = fluent::basicinput::ComboBox;
using Button = fluent::basicinput::Button;
using Label = fluent::textfields::Label;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
Window* FindWindow() {
    for (auto* widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == QStringLiteral("musicBarSettingsWindow"))
            return qobject_cast<Window*>(widget);
    }
    throw std::runtime_error("settings window missing");
}
template<class T> T* Control(Window* window, const char* name) {
    auto* control = window->findChild<T*>(QString::fromLatin1(name));
    Require(control != nullptr, "control missing");
    return control;
}
QRect InRow(QWidget* child, QWidget* row) {
    return {child->mapTo(row, QPoint()), child->size()};
}
void CheckRows(Window* window) {
    const std::array<std::pair<const char*, const char*>, 4> rows{{
        {"positionRow", "positionLockedControl"}, {"lyricsRow", "showLyricsControl"},
        {"sourceRow", "lyricsSourceControl"}, {"audioRow", "captureModeControl"}}};
    for (const auto& [rowName, controlName] : rows) {
        auto* row = Control<fluent::layout::Card>(window, rowName);
        auto* title = row->findChild<Label*>(QString::fromLatin1(rowName) + QStringLiteral("Title"));
        auto* detail = row->findChild<Label*>(QString::fromLatin1(rowName) + QStringLiteral("Description"));
        auto* control = Control<QWidget>(window, controlName);
        Require(title && detail, "row labels missing");
        Require(row->rect().contains(InRow(title, row)), "title clips outside row");
        Require(row->rect().contains(InRow(detail, row)), "description clips outside row");
        Require(row->rect().contains(InRow(control, row)), "control clips outside row");
        Require(!InRow(detail, row).intersects(InRow(control, row)), "description overlaps control");
        Require(!InRow(title, row).intersects(InRow(control, row)), "title overlaps control");
        const auto bounds = detail->fontMetrics().boundingRect(
            QRect(0, 0, detail->width(), 10000), Qt::TextWordWrap, detail->text());
        Require(bounds.height() <= detail->height() + 1, "wrapped description is vertically clipped");
    }
    Require(window->rect().contains(InRow(Control<Button>(window, "resetSettingsButton"), window)),
        "footer clips outside window");
}

void CheckIconRendering() {
    // Loading a font alone did not catch the original HarfBuzz-disabled SDK.
    // Exercise the same paintGlyph path used by window buttons and ComboBox.
    for (const auto& glyph : {Typography::Icons::ChromeClose, Typography::Icons::ChevronDown,
            QStringLiteral("ic_fluent_lock_closed_24_regular")}) {
        QImage image(32, 32, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        {
            QPainter painter(&image);
            painter.setPen(Qt::black);
            Typography::Icons::paintGlyph(painter, QRectF(0, 0, 32, 32), glyph, 24);
        }
        int ink = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (qGray(image.pixel(x, y)) < 128) ++ink;
            }
        }
        Require(ink > 8, "Fluent glyph was loaded but did not render");
    }
}
} // namespace

int main(int argc, char** argv) {
    fluent::prepareHighDpiApplication();
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    if (!fluent::initializeResources()) return 1;
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    // Protect the real user configuration, including the reload/default checks.
    if (!SetEnvironmentVariableW(L"MUSICBAR_TEST_EXECUTABLE_DIRECTORY",
            temporary.path().toStdWString().c_str())) return 1;
    const auto settingsPath = QString::fromStdWString(MusicBar::SettingsStore::GetPath());
    if (!settingsPath.startsWith(temporary.path())) return 1;
    const auto arguments = application.arguments();
    QString artifacts = QDir::currentPath() + QStringLiteral("/settings-verification");
    const int artifactArg = arguments.indexOf(QStringLiteral("--artifacts"));
    if (artifactArg >= 0 && artifactArg + 1 < arguments.size()) artifacts = arguments[artifactArg + 1];
    QDir().mkpath(artifacts);
    const bool hold = arguments.contains(QStringLiteral("--hold"));

    MusicBar::SettingsQtWindow settingsWindow;
    int commits = 0;
    MusicBar::AppSettings last{};
    auto changed = [&](const MusicBar::AppSettings& settings) {
        ++commits;
        last = settings;
        settingsWindow.SetSettings(settings); // same reentrant sync as App
    };
    settingsWindow.Show({}, changed);
    Window* const window = FindWindow();
    if (arguments.contains(QStringLiteral("--preview"))) {
        // A protected configuration and no taskbar/audio instance: preview the
        // real native material without replacing the user's running MusicBar.
        QTimer::singleShot(600000, &application, &QCoreApplication::quit);
        application.exec();
        settingsWindow.Shutdown();
        return 0;
    }
    int step = 0;
    int result = 0;
    QTimer timer;
    timer.setInterval(180);
    auto save = [&](const QString& filename) {
        Require(window->grab().save(artifacts + QLatin1Char('/') + filename), "snapshot failed");
    };
    QObject::connect(&timer, &QTimer::timeout, &application, [&] {
        try {
            auto* lock = Control<Toggle>(window, "positionLockedControl");
            auto* lyrics = Control<Toggle>(window, "showLyricsControl");
            auto* source = Control<Combo>(window, "lyricsSourceControl");
            auto* capture = Control<Combo>(window, "captureModeControl");
            switch (step++) {
            case 0: {
                auto* navigation = Control<fluent::navigation::NavigationView>(window, "settingsNavigation");
                auto* categories = Control<fluent::collections::ListView>(window, "settingsNavigationList");
                Require(categories->model()->rowCount() == 4 && navigation->paneGeometry().width() > 100,
                    "left navigation is absent");
                const int beforeNavigation = commits;
                for (int category = 1; category < 4; ++category) {
                    categories->setCurrentIndex(categories->model()->index(category, 0));
                    for (int section = 0; section < 3; ++section) {
                        const auto name = QStringLiteral("settingsSection%1").arg(section);
                        Require(window->findChild<QWidget*>(name)->isVisible() == (section == category - 1),
                            "navigation displayed the wrong section");
                    }
                }
                categories->setCurrentIndex(categories->model()->index(0, 0));
                Require(commits == beforeNavigation, "navigation changed preferences");
                auto* scroll = Control<fluent::scrolling::ScrollView>(window, "settingsScroll");
                Require(!scroll->viewport()->autoFillBackground() && !scroll->contentWidget()->autoFillBackground(),
                    "opaque scrolling canvas hides the system material");
                if (window->backdropCapabilities().nativeAcrylic) {
                    const auto backdrop = window->backdropState();
                    Require(backdrop.requestedEffect == fluent::windowing::BackdropEffect::Acrylic &&
                        backdrop.backend == fluent::windowing::BackdropBackend::DwmSystemBackdrop &&
                        backdrop.surfaceMode == fluent::windowing::BackdropSurfaceMode::CompositedTransparent,
                        "native Acrylic failed to reach the window");
                }
                Require(window->customWindowChromeEnabled() && window->titleBar()->isVisible(),
                    "caption chrome is absent");
                Require(window->findChild<Button*>(QStringLiteral("fluentWindowCloseButton")) != nullptr,
                    "close button is absent");
                Require(commits == 0, "opening the window submitted settings");
                MusicBar::AppSettings external{false, true, MusicBar::LyricsSource::QQMusic,
                    MusicBar::SpectrumCaptureMode::System};
                settingsWindow.SetSettings(external);
                Require(commits == 0 && lock->isOn() && !lyrics->isOn(), "external sync committed");
                Require(source->currentData().toInt() == static_cast<int>(MusicBar::LyricsSource::QQMusic) &&
                    capture->currentData().toInt() == static_cast<int>(MusicBar::SpectrumCaptureMode::System),
                    "enum sync mismatched");
                lock->setIsOn(false);
                Require(commits == 1 && !last.positionLocked && !last.showLyrics,
                    "toggle failed to submit exactly once");
                for (int value = 0; value < source->count(); ++value) {
                    const int before = commits;
                    source->setCurrentIndex(value);
                    Require(commits == before + 1 && static_cast<int>(last.lyricsSource) ==
                        source->currentData().toInt(), "source item data mismatched");
                }
                capture->setCurrentIndex(0);
                Require(last.fftCaptureMode == MusicBar::SpectrumCaptureMode::Player,
                    "capture item data mismatched");
                Control<Button>(window, "resetSettingsButton")->click();
                Require(last.showLyrics && !last.positionLocked &&
                    last.lyricsSource == MusicBar::LyricsSource::Auto &&
                    last.fftCaptureMode == MusicBar::SpectrumCaptureMode::Player,
                    "restore defaults mismatched");
                QDir().mkpath(QFileInfo(settingsPath).absolutePath());
                QFile invalid(settingsPath);
                Require(invalid.open(QIODevice::WriteOnly), "temporary config failed");
                invalid.write("{broken");
                invalid.close();
                const int before = commits;
                Control<Button>(window, "reloadSettingsButton")->click();
                Require(commits == before && Control<Label>(window, "settingsStatus")->isVisible(),
                    "invalid config was committed");
                Require(MusicBar::SettingsStore::Save(external), "temporary settings save failed");
                Control<Button>(window, "reloadSettingsButton")->click();
                Require(commits == before + 1 && !last.showLyrics && last.positionLocked,
                    "reload did not apply config");
                window->close();
                Require(!settingsWindow.IsOpen(), "close did not hide settings");
                for (int iteration = 0; iteration < 50; ++iteration) {
                    settingsWindow.Show({}, changed);
                    Require(FindWindow() == window, "opening duplicated the window");
                    window->close();
                }
                settingsWindow.Show({}, changed);
                fluent::FluentElement::setTheme(fluent::FluentElement::Light);
                window->setBackdropEffect(fluent::windowing::BackdropEffect::Solid);
                window->resize(1040, 740);
                break;
            }
            case 1:
                CheckRows(window);
                CheckIconRendering();
                Require(QFontInfo(Control<Label>(window, "settingsHeading")->font()).family()
                    .contains(QStringLiteral("YaHei"), Qt::CaseInsensitive), "Chinese UI font regressed");
                {
                    QTextLayout text(QStringLiteral("设置"),
                        Control<Label>(window, "settingsHeading")->font());
                    text.beginLayout();
                    auto line = text.createLine();
                    line.setLineWidth(500);
                    text.endLayout();
                    for (const auto& run : text.glyphRuns()) {
                        const QString family = run.rawFont().familyName();
                        std::fprintf(stdout, "Actual Han face: %s\n", family.toUtf8().constData());
                        Require(family.contains(QStringLiteral("YaHei"), Qt::CaseInsensitive),
                            "Chinese text silently fell back to a different face");
                    }
                }
                save(QStringLiteral("settings-light.png"));
                source->showPopup();
                break;
            case 2: {
                fluent::dialogs_flyouts::Flyout* popup = nullptr;
                for (auto* widget : QApplication::allWidgets()) {
                    if (widget->objectName() == QStringLiteral("ComboBoxPopup") && widget->isVisible())
                        popup = qobject_cast<fluent::dialogs_flyouts::Flyout*>(widget);
                }
                Require(popup && popup->isOpen(), "dropdown did not open");
                Require(popup->grab().save(
                    artifacts + QStringLiteral("/dropdown-light.png")), "popup snapshot failed");
                QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QApplication::sendEvent(QApplication::focusWidget(), &escape);
                Require(settingsWindow.IsOpen() && !popup->isOpen(),
                    "popup Escape hid settings or failed to close popup");
                window->resize(600, 480);
                break;
            }
            case 3:
                CheckRows(window);
                save(QStringLiteral("settings-compact.png"));
                fluent::FluentElement::setTheme(fluent::FluentElement::Dark);
                window->resize(1040, 740);
                break;
            case 4:
                CheckRows(window);
                save(QStringLiteral("settings-dark.png"));
                fluent::FluentElement::setTheme(fluent::FluentElement::HighContrast);
                break;
            case 5:
                CheckRows(window);
                save(QStringLiteral("settings-high-contrast.png"));
                window->showMaximized();
                break;
            case 6:
                Require(window->isMaximized(), "maximize failed");
                CheckRows(window);
                window->showNormal();
                window->showMinimized();
                break;
            case 7:
                Require(window->isMinimized(), "minimize failed");
                settingsWindow.Show({}, changed);
                break;
            case 8: {
                Require(!window->isMinimized() && settingsWindow.IsOpen(), "restore failed");
                const QJsonObject summary{{QStringLiteral("platform"), QGuiApplication::platformName()},
                    {QStringLiteral("devicePixelRatio"), window->devicePixelRatioF()},
                    {QStringLiteral("font"), QFontInfo(Control<Label>(window, "settingsHeading")->font()).family()},
                    {QStringLiteral("commits"), commits}, {QStringLiteral("reopenCycles"), 50},
                    {QStringLiteral("result"), QStringLiteral("passed")}};
                QFile report(artifacts + QStringLiteral("/summary.json"));
                Require(report.open(QIODevice::WriteOnly), "summary failed");
                report.write(QJsonDocument(summary).toJson());
                std::puts("Settings UI: bindings, reload, defaults, popup Escape, caption controls, "
                    "50 reopens, responsive rows and three themes passed.");
                timer.stop();
                if (hold) {
                    fluent::FluentElement::setTheme(fluent::FluentElement::Light);
                    QTimer::singleShot(60000, &application, &QCoreApplication::quit);
                } else application.quit();
                break;
            }
            default: break;
            }
        } catch (const std::exception& error) {
            std::fprintf(stderr, "Settings UI step %d failed: %s\n", step - 1, error.what());
            window->grab().save(artifacts + QStringLiteral("/failure.png"));
            result = 1;
            timer.stop();
            application.quit();
        }
    });
    timer.start();
    application.exec();
    settingsWindow.Shutdown();
    settingsWindow.Shutdown();
    return result;
}
