/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 athre0z
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "Core.hpp"

#include "Config.hpp"
#include "ThemeSelector.hpp"
#include "Settings.hpp"
#include "IdaFontConfig.hpp"

#include <QDir>
#include <QApplication>
#include <QMessageBox>
#include <idp.hpp>
#include <diskio.hpp>
#include <kernwin.hpp>
#include <loader.hpp>

// ========================================================================= //
// [Core]                                                                    //
// ========================================================================= //

Core::Core()
    : m_lastUiActionWasFontChange(false)
{
    // If first start with plugin, ask for theme.
    Settings settings;
    QVariant firstStartVar = settings.value(Settings::kFirstStart, true);
    bool firstStart = true;
    if (firstStartVar.canConvert<bool>())
        firstStart = firstStartVar.toBool();
    else
        settings.remove(Settings::kFirstStart);

    if (firstStartVar.toBool())
    {
        auto pressedButton = QMessageBox::information(qApp->activeWindow(), 
            PLUGIN_NAME": First start",
            PLUGIN_NAME" detected that this is you first IDA startup with this plugin "
            "installed. Do you wish to select a theme now?", 
            QMessageBox::Yes | QMessageBox::No);

        if (pressedButton == QMessageBox::Yes)
            openThemeSelectionDialog();

        settings.setValue(Settings::kFirstStart, false);
    }

    applyStylesheetFromSettings();

    hook_to_notification_point(HT_UI, &uiHook, this);
}

Core::~Core()
{
    unhook_from_notification_point(HT_UI, &uiHook, this);
}

void Core::runPlugin()
{
    openThemeSelectionDialog();

    applyStylesheetFromSettings();
}

bool Core::applyStylesheet(QDir &themeDir)
{
    QString themeDirPath = themeDir.absolutePath();
    QFile stylesheet(themeDirPath + "/stylesheet.qss");
    if (!stylesheet.open(QFile::ReadOnly))
    {
        msg("[" PLUGIN_NAME "] Unable to load stylesheet file.\n");
        return false;
    }

    QString data = stylesheet.readAll();
    preprocessStylesheet(data, themeDirPath);
    qApp->setStyleSheet(data);
    request_refresh(IWID_ALL);
    msg("[" PLUGIN_NAME "] Skin file successfully applied!\n");

    /*
    static bool first = true;
    // Information gathering
    if (!first)
    {
        QFile log(QString(idadir(nullptr)) + "/skin/object_log.log");
        log.open(QFile::WriteOnly);
        std::function<void(QObject*, int)> helper = [&](QObject *element, int depth)
        {
            for (int i = 0; i < depth; ++i)
                log.write("--");
            log.write(element->metaObject()->className());
            log.write(" name: ");
            log.write(element->objectName().toAscii().data());
            if (strcmp(element->metaObject()->className(), "QLabel") == 0)
            {
                log.write("; text: ");
                log.write(((QLabel*)element)->text().toAscii().data());
            }
            if (strcmp(element->metaObject()->className(), "QAbstractButton") == 0)
            {
                log.write("; icon: ");
                log.write(((QAbstractButton*)element)->icon().name().toAscii().data());
            }
            log.write("\n");
            auto children = element->children();
            for (auto it = children.begin(); it != children.end(); ++it)
                helper(*it, depth + 1);
        };
        helper(qApp->activeWindow(), 0);
        log.flush();
        log.close();
    }
    first = false;
    */

    return true;
}

bool Core::applyStylesheetFromSettings()
{
    QDir activeThemeDir;
    if (Utils::getCurrentThemeDir(activeThemeDir))
        return applyStylesheet(activeThemeDir);
    return false;
}

void Core::preprocessStylesheet(QString &qss, const QString &themeDirPath)
{
    qss.replace("<IDADIR>", idadir(nullptr));
    qss.replace("<SKINDIR>", themeDirPath);

    auto applyFontReplacements = [&](const QString &keyword, IdaFontConfig::FontType type)
    {
        IdaFontConfig settings(type);
        QString prefix = "<" + keyword + "_FONT_";

        qss.replace(prefix + "FAMILY>", settings.family());
        qss.replace(prefix + "STYLE>", settings.italic() ? " italic" : "");
        qss.replace(prefix + "WEIGHT>", settings.bold() ? " bold" : "");
        qss.replace(prefix + "SIZE>", QString::number(settings.size()) + "pt");
    };

    applyFontReplacements("DISASSEMBLY",     IdaFontConfig::FONT_DISASSEMBLY);
    applyFontReplacements("HEXVIEW",         IdaFontConfig::FONT_HEXVIEW);
    applyFontReplacements("DEBUG_REGISTERS", IdaFontConfig::FONT_DEBUG_REGISTERS);
    applyFontReplacements("TEXT_INPUT",      IdaFontConfig::FONT_TEXT_INPUT);
    applyFontReplacements("OUTPUT_WINDOW",   IdaFontConfig::FONT_OUTPUT_WINDOW);

    //msg("%s\n", qss.toAscii().data());
}

int Core::uiHook(void *userData, int notificationCode, va_list va)
{
    auto thiz = static_cast<Core*>(userData);
    Q_ASSERT(thiz);

    switch (notificationCode)
    {
        case ui_preprocess:
        {
            const char *action = va_arg(va, const char*);
            if (::qstrcmp(action, "SetFont") == 0)
                thiz->m_lastUiActionWasFontChange = true;
        } break;

        case ui_postprocess:
        {
            if (thiz->m_lastUiActionWasFontChange)
            {
                QMessageBox::warning(qApp->activeWindow(), "IDASkins",
                    "Please note that altering the font settings when IDASkins is loaded "
                    "may cause strange effects on font rendering. It is recommended to "
                    "restart IDA after making font-related changes in the settings to avoid "
                    "instability.");
                thiz->m_lastUiActionWasFontChange = false;
            }
        } break;
    }

    return 0;
}

void Core::openThemeSelectionDialog()
{
    ThemeSelector selector(qApp->activeWindow());
    connect(&selector, SIGNAL(accepted()), SLOT(onThemeSelectionAccepted()));
    selector.exec();
}

void Core::onThemeSelectionAccepted()
{
    auto selector = dynamic_cast<ThemeSelector*>(sender());
    Q_ASSERT(selector);

    // New theme selected? Save to settings.
    if (selector->selectedThemeDir())
    {
        Settings().setValue(Settings::kSelectedThemeDir, 
            selector->selectedThemeDir()->dirName());
    }
}

// ========================================================================= //
