#include "audio/AudioEngine.h"
#include "audio/WasapiCaptureBackend.h"
#include "settings/AppSettings.h"
#include "ui/MainWindow.h"
#include "ui/SonarcLookAndFeel.h"
#include "ui/Theme.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace sonarc {

class SonarcApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Sonarc"; }
    const juce::String getApplicationVersion() override { return SONARC_VERSION; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        settings = std::make_unique<AppSettings>();

        // Colours first, so every component is created with the user's theme.
        theme::apply(theme::fromVar(settings->theme(), theme::presetNamed(settings->themePreset())));
        lookAndFeel.refreshColours();
        juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);
        engine = std::make_unique<AudioEngine>(std::make_unique<WasapiCaptureBackend>());
        mainWindow = std::make_unique<MainWindow>(*engine, *settings);
    }

    void shutdown() override
    {
        if (mainWindow != nullptr)
            settings->setWindowState(mainWindow->windowStateForSaving());

        mainWindow.reset();
        engine.reset();

        if (settings != nullptr)
            settings->save();
        settings.reset();

        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    SonarcLookAndFeel lookAndFeel;
    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace sonarc

START_JUCE_APPLICATION(sonarc::SonarcApplication)
