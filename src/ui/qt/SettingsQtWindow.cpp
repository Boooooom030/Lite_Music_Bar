// FluentQt must precede Windows headers (rpcndr.h defines the macro "small").
#include <FluentQt/FluentQt.h>
#include "SettingsQtWindow.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <windows.h>

#if !QT_CONFIG(harfbuzz)
#error "MusicBar requires Qt Gui with HarfBuzz: enable qtbase[harfbuzz] and rebuild the static SDK."
#endif

namespace MusicBar {
namespace {
constexpr int kInitialWidth = 1040;
constexpr int kInitialHeight = 740;
constexpr int kMinimumWidth = 600;
constexpr int kMinimumHeight = 480;
constexpr int kContentMargin = 28;
constexpr int kControlWidth = 208;
constexpr int kRowPadding = 18;
constexpr int kIconWidth = 24;
constexpr int kIconGap = 16;
constexpr int kControlGap = 24;
constexpr int kStackedControlGap = 12;
constexpr int kCompactRowWidth = 660;

using Label = fluent::textfields::Label;
using ColorRole = Label::TextColorRole;

QFont SettingsFont(Typography::FontRole role) {
    QFont font = Typography::fontStyle(role).toQFont();
    font.setFamily(QApplication::font().family());
    // Inter's exact face names must not override the native face's weight.
    font.setStyleName({});
    font.setHintingPreference(QFont::PreferDefaultHinting);
    font.setStyleStrategy(QFont::PreferDefault);
    return font;
}

Label* MakeLabel(const QString& text, Typography::FontRole role,
    ColorRole color, QWidget* parent) {
    auto* label = new Label(text, parent);
    label->setFluentTypography(role);
    label->setFont(SettingsFont(role));
    label->setTextColorRole(color);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setMinimumWidth(0);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    return label;
}

void ConfigureTypography() {
    // Bundled Inter has no Han glyphs. Prefer a Windows UI face for the entire
    // Chinese interface, including painted controls and their popup delegates.
    const QStringList families = QFontDatabase::families();
    for (const QString& family : {QStringLiteral("Microsoft YaHei UI"),
            QStringLiteral("Microsoft YaHei"), QStringLiteral("DengXian")}) {
        if (!families.contains(family, Qt::CaseInsensitive)) continue;
        fluent::ThemeRegistry::instance().setFontFamilyOverride(family);
        QFontDatabase::addApplicationFallbackFontFamily(QChar::Script_Han, family);
        QFont font = Typography::fontStyle(Typography::FontRole::Body).toQFont();
        font.setFamily(family);
        font.setStyleName({});
        font.setHintingPreference(QFont::PreferDefaultHinting);
        font.setStyleStrategy(QFont::PreferDefault);
        QApplication::setFont(font);
        break;
    }
}

fluent::FontIcon* MakeIcon(const char* name, int size, QWidget* parent) {
    auto* icon = new fluent::FontIcon(QString::fromLatin1(name), parent);
    icon->setIconSize(size);
    icon->setFixedSize(size, size);
    icon->setAttribute(Qt::WA_TransparentForMouseEvents);
    return icon;
}

// ScrollView normally fills its viewport with opaque bgCanvas, which hides
// the system material. Keep the library's scrolling and make only the host
// surface transparent; controls and cards keep their normal Fluent painting.
class MaterialScrollView final : public fluent::scrolling::ScrollView {
public:
    explicit MaterialScrollView(QWidget* parent) : ScrollView(parent) { ClearFill(); }
    void onThemeUpdated() override {
        ScrollView::onThemeUpdated();
        ClearFill();
    }
private:
    void ClearFill() {
        auto palette = viewport()->palette();
        palette.setColor(QPalette::Window, Qt::transparent);
        palette.setColor(QPalette::Base, Qt::transparent);
        viewport()->setPalette(palette);
        viewport()->setAutoFillBackground(false);
    }
};

// Composition only: Card, Label and the input control retain library painting.
// Height-for-width keeps wrapped Chinese text and stacked controls visible.
class SettingsRow final : public fluent::layout::Card {
public:
    SettingsRow(const QString& name, const char* iconName, const QString& title,
        const QString& description, QWidget* control, QWidget* parent)
        : Card(parent) {
        setObjectName(name);
        setAppearance(Card::Layer);
        // Opaque white stands out when active Acrylic becomes darker. Tint
        // only the light-theme surface, leaving text and input opacity intact.
        setThemeOverrides({{QStringLiteral("light"), QJsonObject{
            {QStringLiteral("bgLayer"), QStringLiteral("#FFFFFFB3")}}}});
        setBorderVisible(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setMinimumHeight(76);
        m_grid = new QGridLayout(this);
        m_grid->setContentsMargins(kRowPadding, kRowPadding, kRowPadding, kRowPadding);
        m_grid->setHorizontalSpacing(kIconGap);
        m_grid->setVerticalSpacing(kStackedControlGap);

        auto* icon = MakeIcon(iconName, kIconWidth, this);
        m_grid->addWidget(icon, 0, 0, Qt::AlignVCenter);
        m_text = new QWidget(this);
        m_text->setMinimumWidth(0);
        m_text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto* textLayout = new QVBoxLayout(m_text);
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(4);
        auto* heading = MakeLabel(title, Typography::FontRole::BodyStrong,
            ColorRole::Primary, m_text);
        heading->setObjectName(name + QStringLiteral("Title"));
        auto* detail = MakeLabel(description, Typography::FontRole::Body,
            ColorRole::Secondary, m_text);
        detail->setObjectName(name + QStringLiteral("Description"));
        textLayout->addWidget(heading);
        textLayout->addWidget(detail);

        m_control = new QWidget(this);
        m_control->setMinimumWidth(kControlWidth);
        auto* controlLayout = new QHBoxLayout(m_control);
        controlLayout->setContentsMargins(0, 0, 0, 0);
        controlLayout->setSpacing(0);
        controlLayout->addStretch();
        controlLayout->addWidget(control, 0, Qt::AlignRight | Qt::AlignVCenter);
        Arrange(false);
    }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override {
        const bool compact = width < kCompactRowWidth;
        const int bodyWidth = width - 2 * kRowPadding - kIconWidth - kIconGap;
        const int textWidth = std::max(1, bodyWidth -
            (compact ? 0 : kControlWidth + kControlGap));
        const int textHeight = std::max(m_text->minimumSizeHint().height(),
            m_text->layout()->totalHeightForWidth(textWidth));
        const int controlHeight = m_control->sizeHint().height();
        return 2 * kRowPadding + (compact
            ? std::max(kIconWidth, textHeight) + kStackedControlGap + controlHeight
            : std::max({kIconWidth, textHeight, controlHeight}));
    }
    QSize sizeHint() const override { return {700, heightForWidth(700)}; }
    QSize minimumSizeHint() const override { return {300, 76}; }

protected:
    void resizeEvent(QResizeEvent* event) override {
        const bool compact = event->size().width() < kCompactRowWidth;
        if (compact != m_compact) Arrange(compact);
        Card::resizeEvent(event);
    }

private:
    void Arrange(bool compact) {
        m_compact = compact;
        m_grid->removeWidget(m_text);
        m_grid->removeWidget(m_control);
        m_grid->setColumnStretch(1, 1);
        m_grid->setColumnStretch(2, 0);
        m_grid->addWidget(m_text, 0, 1, 1, compact ? 2 : 1);
        if (compact) {
            m_grid->addWidget(m_control, 1, 1, 1, 2, Qt::AlignRight);
        } else {
            m_grid->addWidget(m_control, 0, 2, Qt::AlignVCenter);
        }
        m_grid->invalidate();
        updateGeometry();
    }
    QGridLayout* m_grid = nullptr;
    QWidget* m_text = nullptr;
    QWidget* m_control = nullptr;
    bool m_compact = false;
};

fluent::basicinput::ToggleSwitch* MakeToggle(const QString& name,
    const QString& accessibleName) {
    auto* toggle = new fluent::basicinput::ToggleSwitch();
    toggle->setObjectName(name);
    toggle->setOnContent(QStringLiteral("开启"));
    toggle->setOffContent(QStringLiteral("关闭"));
    toggle->setAccessibleName(accessibleName);
    toggle->setFont(SettingsFont(Typography::FontRole::Body));
    toggle->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return toggle;
}

fluent::basicinput::ComboBox* MakeCombo(const QString& name,
    const QString& accessibleName) {
    auto* combo = new fluent::basicinput::ComboBox();
    combo->setObjectName(name);
    combo->setAccessibleName(accessibleName);
    combo->setFont(SettingsFont(Typography::FontRole::Body));
    combo->setEditable(false);
    combo->setFixedWidth(kControlWidth);
    combo->setMinimumHeight(32);
    return combo;
}

fluent::FluentElement::Theme ResolveSystemTheme() {
    HIGHCONTRASTW highContrast{};
    highContrast.cbSize = sizeof(highContrast);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(highContrast), &highContrast, 0) &&
        (highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0) {
        return fluent::FluentElement::HighContrast;
    }
    DWORD light = 1;
    DWORD size = sizeof(light);
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &key) == ERROR_SUCCESS) {
        RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
            reinterpret_cast<BYTE*>(&light), &size);
        RegCloseKey(key);
    }
    return light != 0 ? fluent::FluentElement::Light : fluent::FluentElement::Dark;
}
} // namespace

SettingsQtWindow::SettingsQtWindow(QObject* parent) : QObject(parent) {}
SettingsQtWindow::~SettingsQtWindow() { Shutdown(); }

void SettingsQtWindow::Show(const AppSettings& settings, ChangeHandler handler) {
    m_onChanged = std::move(handler);
    if (!m_window) BuildWindow();
    m_settings = settings;
    SyncFromSettings(settings);
    UpdateThemeFromSystem();
    if (!m_placed) {
        PlaceOnActiveScreen();
        m_placed = true;
    }
    if (m_window->isMinimized()) m_window->showNormal();
    m_window->prepareForNativeRestore();
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
    m_window->requestForegroundActivation();
    UpdateBackdrop();
}

void SettingsQtWindow::SetSettings(const AppSettings& settings) {
    m_settings = settings;
    if (m_window) SyncFromSettings(settings);
}

bool SettingsQtWindow::IsOpen() const { return m_window && m_window->isVisible(); }

void SettingsQtWindow::OnSystemThemeChanged() {
    UpdateThemeFromSystem();
    UpdateBackdrop();
}

void SettingsQtWindow::Shutdown() {
    CloseOpenPopups();
    if (m_window) m_window->removeEventFilter(this);
    m_window.reset();
    m_lockSwitch = nullptr;
    m_showLyricsSwitch = nullptr;
    m_sourceCombo = nullptr;
    m_captureCombo = nullptr;
    m_reloadButton = nullptr;
    m_resetButton = nullptr;
    m_statusLabel = nullptr;
    m_navigation = nullptr;
    m_navigationList = nullptr;
    m_pageHeading = nullptr;
    m_pageDescription = nullptr;
    for (auto*& section : m_sections) section = nullptr;
    m_onChanged = {};
    m_placed = false;
}

void SettingsQtWindow::BuildWindow() {
    ConfigureTypography();
    UpdateThemeFromSystem();
    m_window = std::make_unique<fluent::windowing::Window>();
    m_window->setObjectName(QStringLiteral("musicBarSettingsWindow"));
    m_window->setWindowTitle(QStringLiteral("MusicBar 设置"));
    m_window->setWindowIcon(QIcon(QStringLiteral(":/assets/icon.ico")));
    // Window opts into caption buttons only when custom chrome is enabled.
    // Without this, the previous build had no title bar or close button.
    m_window->setCustomWindowChromeEnabled(true);
    m_window->setCaptionButtonToolTips(QStringLiteral("最小化"), QStringLiteral("最大化"),
        QStringLiteral("关闭"), QStringLiteral("还原"));
    m_window->setCaptionButtonAccessibleNames(QStringLiteral("最小化"), QStringLiteral("最大化"),
        QStringLiteral("关闭设置"), QStringLiteral("还原"));
    m_window->titleBar()->setTitleBarHeight(44);
    m_window->setMinimumSize(kMinimumWidth, kMinimumHeight);
    m_window->resize(kInitialWidth, kInitialHeight);
    m_window->setAttribute(Qt::WA_DeleteOnClose, false);
    m_window->installEventFilter(this);
    // Prepare the alpha surface before the first native frame, like Gallery.
    UpdateBackdrop();

    auto* caption = new QWidget(m_window->titleBar());
    caption->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* captionLayout = new QHBoxLayout(caption);
    captionLayout->setContentsMargins(16, 0, 8, 0);
    captionLayout->setSpacing(10);
    auto* appIcon = new QLabel(caption);
    appIcon->setObjectName(QStringLiteral("settingsAppIcon"));
    appIcon->setFixedSize(18, 18);
    UpdateWindowIcon();
    captionLayout->addWidget(appIcon);
    captionLayout->addWidget(MakeLabel(QStringLiteral("MusicBar"),
        Typography::FontRole::Body, ColorRole::Primary, caption));
    captionLayout->addStretch();
    m_window->titleBar()->setContentWidget(caption);

    m_navigation = new fluent::navigation::NavigationView(m_window.get());
    m_navigation->setObjectName(QStringLiteral("settingsNavigation"));
    m_navigation->setDisplayMode(fluent::navigation::NavigationView::DisplayMode::Left);
    m_navigation->setExpandedPaneWidth(240);
    m_navigation->setPaneOpen(true);

    auto* brand = new QWidget(m_navigation);
    auto* brandLayout = new QVBoxLayout(brand);
    brandLayout->setContentsMargins(24, 24, 20, 24);
    brandLayout->setSpacing(6);
    auto* identity = new QHBoxLayout();
    identity->setSpacing(12);
    auto* brandIcon = new QLabel(brand);
    brandIcon->setObjectName(QStringLiteral("settingsBrandIcon"));
    brandIcon->setFixedSize(40, 40);
    identity->addWidget(brandIcon);
    auto* brandText = new QVBoxLayout();
    brandText->setSpacing(2);
    brandText->addWidget(MakeLabel(QStringLiteral("MusicBar"), Typography::FontRole::BodyStrong,
        ColorRole::Primary, brand));
    brandText->addWidget(MakeLabel(QStringLiteral("任务栏音乐控制"), Typography::FontRole::Caption,
        ColorRole::Secondary, brand));
    identity->addLayout(brandText, 1);
    brandLayout->addLayout(identity);
    m_navigation->setHeaderChromeWidget(brand, fluent::WidgetOwnership::Owned);

    auto* pane = new QWidget(m_navigation);
    auto* paneLayout = new QVBoxLayout(pane);
    paneLayout->setContentsMargins(12, 0, 12, 0);
    paneLayout->setSpacing(0);
    m_navigationList = new fluent::collections::ListView(pane);
    m_navigationList->setObjectName(QStringLiteral("settingsNavigationList"));
    m_navigationList->setAccessibleName(QStringLiteral("设置分类"));
    m_navigationList->setBorderVisible(false);
    m_navigationList->setBackgroundVisible(false);
    m_navigationList->setFont(SettingsFont(Typography::FontRole::Body));
    m_navigationList->setIconSize(QSize(20, 20));
    m_navigationList->setSpacing(2);
    m_navigationList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto* categories = new QStandardItemModel(m_navigationList);
    for (const auto& title : {QStringLiteral("全部设置"), QStringLiteral("任务栏"),
            QStringLiteral("歌词"), QStringLiteral("音频")}) {
        auto* item = new QStandardItem(title);
        item->setSizeHint(QSize(150, 42));
        categories->appendRow(item);
    }
    m_navigationList->setModel(categories);
    paneLayout->addWidget(m_navigationList);
    m_navigation->setMainChromeWidget(pane, fluent::WidgetOwnership::Owned);

    auto* paneFooter = new QWidget(m_navigation);
    auto* paneFooterLayout = new QVBoxLayout(paneFooter);
    paneFooterLayout->setContentsMargins(24, 12, 20, 24);
    paneFooterLayout->setSpacing(4);
    paneFooterLayout->addWidget(MakeLabel(QStringLiteral("更改自动保存"), Typography::FontRole::Caption,
        ColorRole::Secondary, paneFooter));
    m_navigation->setFooterChromeWidget(paneFooter, fluent::WidgetOwnership::Owned);

    auto* surface = new QWidget(m_navigation->contentHost());
    surface->setObjectName(QStringLiteral("settingsContent"));
    auto* root = new QVBoxLayout(surface);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* header = new QWidget(surface);
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(kContentMargin, 28, kContentMargin, 24);
    headerLayout->setSpacing(6);
    m_pageHeading = MakeLabel(QStringLiteral("设置"), Typography::FontRole::Title,
        ColorRole::Primary, header);
    m_pageHeading->setObjectName(QStringLiteral("settingsHeading"));
    headerLayout->addWidget(m_pageHeading);
    m_pageDescription = MakeLabel(QStringLiteral("管理音乐栏的位置、歌词和音频采集。"),
        Typography::FontRole::Body, ColorRole::Secondary, header);
    headerLayout->addWidget(m_pageDescription);
    root->addWidget(header);

    m_lockSwitch = MakeToggle(QStringLiteral("positionLockedControl"), QStringLiteral("锁定位置"));
    m_showLyricsSwitch = MakeToggle(QStringLiteral("showLyricsControl"), QStringLiteral("显示歌词"));
    m_sourceCombo = MakeCombo(QStringLiteral("lyricsSourceControl"), QStringLiteral("歌词来源"));
    const std::array<std::pair<const char*, LyricsSource>, 6> sources{{
        {"自动选择", LyricsSource::Auto}, {"网易云音乐", LyricsSource::NetEase},
        {"QQ 音乐", LyricsSource::QQMusic}, {"酷狗音乐", LyricsSource::Kugou},
        {"汽水音乐", LyricsSource::Soda}, {"LRCLIB", LyricsSource::Lrclib}}};
    for (const auto& [label, value] : sources) {
        m_sourceCombo->addItem(QString::fromUtf8(label), static_cast<int>(value));
    }
    m_captureCombo = MakeCombo(QStringLiteral("captureModeControl"), QStringLiteral("音频采集范围"));
    m_captureCombo->addItem(QStringLiteral("当前播放器"), static_cast<int>(SpectrumCaptureMode::Player));
    m_captureCombo->addItem(QStringLiteral("系统声音"), static_cast<int>(SpectrumCaptureMode::System));

    auto* page = new QWidget();
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(kContentMargin, 0, kContentMargin, 20);
    pageLayout->setSpacing(20);
    auto addSection = [&](int index, const QString& title) {
        auto* section = new QWidget(page);
        section->setObjectName(QStringLiteral("settingsSection%1").arg(index));
        m_sections[index] = section;
        auto* layout = new QVBoxLayout(section);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        layout->addWidget(MakeLabel(title, Typography::FontRole::BodyStrong,
            ColorRole::Primary, section));
        pageLayout->addWidget(section);
        return layout;
    };
    auto* taskbarSection = addSection(0, QStringLiteral("位置与交互"));
    taskbarSection->addWidget(new SettingsRow(QStringLiteral("positionRow"),
        "ic_fluent_lock_closed_24_regular", QStringLiteral("锁定位置"),
        QStringLiteral("锁定后无法拖动音乐栏，播放按钮仍可使用。"), m_lockSwitch, page));
    auto* lyricsSection = addSection(1, QStringLiteral("歌词显示"));
    lyricsSection->addWidget(new SettingsRow(QStringLiteral("lyricsRow"),
        "ic_fluent_text_quote_24_regular", QStringLiteral("显示歌词"),
        QStringLiteral("在音乐栏中显示与歌曲同步的歌词。"), m_showLyricsSwitch, page));
    lyricsSection->addWidget(new SettingsRow(QStringLiteral("sourceRow"),
        "ic_fluent_cloud_24_regular", QStringLiteral("歌词来源"),
        QStringLiteral("自动选择会优先匹配当前播放器。"), m_sourceCombo, page));
    auto* audioSection = addSection(2, QStringLiteral("频谱采集"));
    audioSection->addWidget(new SettingsRow(QStringLiteral("audioRow"),
        "ic_fluent_speaker_2_24_regular", QStringLiteral("音频采集范围"),
        QStringLiteral("用于频谱分析；旧版 Windows 会使用系统声音。"), m_captureCombo, page));
    pageLayout->addStretch();

    auto* scroll = new MaterialScrollView(surface);
    scroll->setObjectName(QStringLiteral("settingsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarVisibility(fluent::scrolling::ScrollView::ScrollBarVisibility::Disabled);
    scroll->setHorizontalScrollMode(fluent::scrolling::ScrollView::ScrollMode::Disabled);
    scroll->setContentWidget(page, fluent::WidgetOwnership::Owned);
    // QScrollArea enables this on the hosted widget as well as its viewport.
    page->setAutoFillBackground(false);
    root->addWidget(scroll, 1);

    auto* footer = new QWidget(surface);
    auto* footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(kContentMargin, 12, kContentMargin, 20);
    footerLayout->setSpacing(8);
    m_statusLabel = MakeLabel(QString(), Typography::FontRole::Body,
        ColorRole::Primary, footer);
    m_statusLabel->setObjectName(QStringLiteral("settingsStatus"));
    m_statusLabel->setAccessibleName(QStringLiteral("设置读取结果"));
    m_statusLabel->hide();
    footerLayout->addWidget(m_statusLabel);
    auto* actions = new QHBoxLayout();
    actions->setSpacing(8);
    actions->addStretch();
    m_reloadButton = new fluent::basicinput::Button(QStringLiteral("重新读取"), footer);
    m_reloadButton->setObjectName(QStringLiteral("reloadSettingsButton"));
    m_reloadButton->setAccessibleName(QStringLiteral("重新读取设置文件"));
    m_reloadButton->setFluentStyle(fluent::basicinput::Button::Subtle);
    m_resetButton = new fluent::basicinput::Button(QStringLiteral("恢复默认"), footer);
    m_resetButton->setObjectName(QStringLiteral("resetSettingsButton"));
    m_resetButton->setAccessibleName(QStringLiteral("恢复默认设置"));
    for (auto* button : {m_reloadButton, m_resetButton}) {
        button->setFont(SettingsFont(Typography::FontRole::Body));
        button->setMinimumSize(112, 32);
        actions->addWidget(button);
    }
    footerLayout->addLayout(actions);
    root->addWidget(footer);
    m_navigation->contentHost()->insertPage(0, surface, fluent::WidgetOwnership::Owned);
    m_navigation->contentHost()->setCurrentIndex(0, 0, false);
    m_window->setContentWidget(m_navigation);
    connect(m_navigationList->selectionModel(), &QItemSelectionModel::currentChanged,
        this, [this](const QModelIndex& current, const QModelIndex&) {
            if (current.isValid()) SelectCategory(current.row());
        });
    m_navigationList->setCurrentIndex(categories->index(0, 0));
    UpdateNavigationIcons();
    UpdateWindowIcon();
    ConnectSignals();
    QWidget::setTabOrder(m_lockSwitch, m_showLyricsSwitch);
    QWidget::setTabOrder(m_showLyricsSwitch, m_sourceCombo);
    QWidget::setTabOrder(m_sourceCombo, m_captureCombo);
    QWidget::setTabOrder(m_captureCombo, m_reloadButton);
    QWidget::setTabOrder(m_reloadButton, m_resetButton);
}

void SettingsQtWindow::ConnectSignals() {
    connect(m_lockSwitch, &fluent::basicinput::ToggleSwitch::toggled,
        this, [this](bool) { CommitFromControls(); });
    connect(m_showLyricsSwitch, &fluent::basicinput::ToggleSwitch::toggled,
        this, [this](bool) { CommitFromControls(); });
    connect(m_sourceCombo, qOverload<int>(&QComboBox::currentIndexChanged),
        this, [this](int) { CommitFromControls(); });
    connect(m_captureCombo, qOverload<int>(&QComboBox::currentIndexChanged),
        this, [this](int) { CommitFromControls(); });
    connect(m_reloadButton, &fluent::basicinput::Button::clicked,
        this, &SettingsQtWindow::ReloadFromFile);
    connect(m_resetButton, &fluent::basicinput::Button::clicked,
        this, &SettingsQtWindow::RestoreDefaults);
}

void SettingsQtWindow::SyncFromSettings(const AppSettings& settings) {
    const QSignalBlocker lockBlocker(m_lockSwitch);
    const QSignalBlocker lyricsBlocker(m_showLyricsSwitch);
    const QSignalBlocker sourceBlocker(m_sourceCombo);
    const QSignalBlocker captureBlocker(m_captureCombo);
    m_lockSwitch->setIsOn(settings.positionLocked);
    m_showLyricsSwitch->setIsOn(settings.showLyrics);
    const int source = m_sourceCombo->findData(static_cast<int>(settings.lyricsSource));
    const int capture = m_captureCombo->findData(static_cast<int>(settings.fftCaptureMode));
    if (source >= 0) m_sourceCombo->setCurrentIndex(source);
    if (capture >= 0) m_captureCombo->setCurrentIndex(capture);
}

bool SettingsQtWindow::ReadControls(AppSettings& settings) const {
    const QVariant source = m_sourceCombo->currentData();
    const QVariant capture = m_captureCombo->currentData();
    bool sourceValid = false;
    bool captureValid = false;
    const int sourceValue = source.toInt(&sourceValid);
    const int captureValue = capture.toInt(&captureValid);
    if (!sourceValid || !captureValid ||
        sourceValue < static_cast<int>(LyricsSource::Auto) ||
        sourceValue > static_cast<int>(LyricsSource::Lrclib) ||
        captureValue < static_cast<int>(SpectrumCaptureMode::Player) ||
        captureValue > static_cast<int>(SpectrumCaptureMode::System)) return false;
    settings.positionLocked = m_lockSwitch->isOn();
    settings.showLyrics = m_showLyricsSwitch->isOn();
    settings.lyricsSource = static_cast<LyricsSource>(sourceValue);
    settings.fftCaptureMode = static_cast<SpectrumCaptureMode>(captureValue);
    return true;
}

void SettingsQtWindow::CommitFromControls() {
    AppSettings settings{};
    if (!ReadControls(settings)) return;
    if (settings.positionLocked == m_settings.positionLocked &&
        settings.showLyrics == m_settings.showLyrics &&
        settings.lyricsSource == m_settings.lyricsSource &&
        settings.fftCaptureMode == m_settings.fftCaptureMode) return;
    m_settings = settings;
    PublishStatus({});
    if (m_onChanged) m_onChanged(m_settings);
}

void SettingsQtWindow::ReloadFromFile() {
    AppSettings loaded{};
    if (SettingsStore::GetFileStamp() != 0 && !SettingsStore::Load(loaded)) {
        PublishStatus(QStringLiteral("设置文件无法读取，请检查内容。"));
        return;
    }
    PublishStatus({});
    m_settings = loaded;
    SyncFromSettings(loaded);
    if (m_onChanged) m_onChanged(m_settings);
}

void SettingsQtWindow::RestoreDefaults() {
    m_settings = {};
    SyncFromSettings(m_settings);
    PublishStatus({});
    if (m_onChanged) m_onChanged(m_settings);
}

void SettingsQtWindow::UpdateThemeFromSystem() {
    const auto theme = ResolveSystemTheme();
    if (m_appliedTheme == static_cast<int>(theme)) return;
    m_appliedTheme = static_cast<int>(theme);
    fluent::FluentElement::setTheme(theme);
    UpdateNavigationIcons();
}

void SettingsQtWindow::UpdateBackdrop() {
    if (!m_window) return;
    m_window->setBackdropEffect(
        fluent::FluentElement::currentTheme() == fluent::FluentElement::HighContrast
            ? fluent::windowing::BackdropEffect::Solid
            : fluent::windowing::BackdropEffect::Acrylic);
    m_window->reapplySystemBackdrop();
    // Windows 11 uses DWM's native material. Older systems use the library's
    // opaque material fallback, avoiding unsupported transparent black frames.
}

void SettingsQtWindow::Hide() {
    CloseOpenPopups();
    if (m_window) m_window->hide();
}

void SettingsQtWindow::UpdateWindowIcon() {
    if (!m_window) return;
    auto* icon = m_window->findChild<QLabel*>(QStringLiteral("settingsAppIcon"));
    if (icon) icon->setPixmap(m_window->windowIcon().pixmap(
        QSize(18, 18), m_window->devicePixelRatioF()));
    auto* brand = m_window->findChild<QLabel*>(QStringLiteral("settingsBrandIcon"));
    if (brand) brand->setPixmap(m_window->windowIcon().pixmap(
        QSize(40, 40), m_window->devicePixelRatioF()));
}

void SettingsQtWindow::UpdateNavigationIcons() {
    if (!m_navigationList || !m_window) return;
    const std::array<const char*, 4> glyphs{{"ic_fluent_settings_24_regular",
        "ic_fluent_desktop_24_regular", "ic_fluent_text_quote_24_regular",
        "ic_fluent_speaker_2_24_regular"}};
    auto* model = qobject_cast<QStandardItemModel*>(m_navigationList->model());
    if (!model) return;
    const qreal dpr = m_window->devicePixelRatioF();
    for (int index = 0; index < 4; ++index) {
        QPixmap image(QSize(qRound(20 * dpr), qRound(20 * dpr)));
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setPen(m_navigationList->themeColorsRef().textPrimary);
        Typography::Icons::paintGlyph(painter, QRectF(0, 0, 20, 20),
            QString::fromLatin1(glyphs[index]), 20);
        painter.end();
        model->item(index)->setIcon(QIcon(image));
    }
    m_navigationList->setFont(SettingsFont(Typography::FontRole::Body));
}

void SettingsQtWindow::SelectCategory(int index) {
    if (index < 0 || index > 3) return;
    CloseOpenPopups();
    const std::array<QString, 4> titles{{QStringLiteral("设置"), QStringLiteral("任务栏"),
        QStringLiteral("歌词"), QStringLiteral("音频")}};
    const std::array<QString, 4> descriptions{{QStringLiteral("管理音乐栏的位置、歌词和音频采集。"),
        QStringLiteral("调整音乐栏在任务栏上的位置与交互。"),
        QStringLiteral("设置同步歌词的显示与来源。"),
        QStringLiteral("选择用于音乐频谱的声音采集范围。")}};
    m_pageHeading->setText(titles[index]);
    m_pageDescription->setText(descriptions[index]);
    for (int section = 0; section < 3; ++section)
        m_sections[section]->setVisible(index == 0 || index == section + 1);
    auto* scroll = m_window->findChild<fluent::scrolling::ScrollView*>(QStringLiteral("settingsScroll"));
    if (scroll) scroll->verticalScrollBar()->setValue(0);
}

void SettingsQtWindow::PlaceOnActiveScreen() {
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen || !m_window) return;
    const QRect available = screen->availableGeometry();
    // A small/high-DPI work area may be smaller than the usual minimum.
    // The body scrolls; caption buttons and footer stay accessible.
    m_window->setMinimumSize(std::min(kMinimumWidth, available.width()),
        std::min(kMinimumHeight, available.height()));
    const QSize size = m_window->size().boundedTo(available.size());
    m_window->setGeometry(available.x() + (available.width() - size.width()) / 2,
        available.y() + (available.height() - size.height()) / 2,
        size.width(), size.height());
}

void SettingsQtWindow::CloseOpenPopups() {
    for (auto* combo : {m_sourceCombo, m_captureCombo}) {
        if (combo) combo->hidePopup();
    }
}

void SettingsQtWindow::PublishStatus(const QString& message) {
    if (!m_statusLabel) return;
    m_statusLabel->setText(message);
    m_statusLabel->setVisible(!message.isEmpty());
}

bool SettingsQtWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_window.get()) {
        if (event->type() == QEvent::Close) {
            event->ignore();
            Hide();
            return true;
        }
        if (event->type() == QEvent::KeyPress &&
            static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            if (QApplication::activePopupWidget()) CloseOpenPopups();
            else Hide();
            return true;
        }
        if (event->type() == QEvent::ThemeChange) OnSystemThemeChanged();
        if (event->type() == QEvent::DevicePixelRatioChange) {
            UpdateWindowIcon();
            UpdateNavigationIcons();
        }
        if (event->type() == QEvent::Resize && m_navigation) {
            m_navigation->setExpandedPaneWidth(m_window->width() < 850 ? 180 : 240);
        }
    }
    return QObject::eventFilter(watched, event);
}
} // namespace MusicBar
