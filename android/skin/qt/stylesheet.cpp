//Copyright (C) 2016 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "android/skin/qt/stylesheet.h"

#include <atomic>
#include <QFile>
#include <QHash>
#include <QMutex>
#include <QTextStream>
#include <QtGlobal>

namespace Ui {

const char* THEME_PATH_VAR = "PATH";
const char* MAJOR_TAB_COLOR_VAR = "MAJOR_TAB_COLOR";
const char* TAB_BKG_COLOR_VAR = "TAB_BKG_COLOR";
const char* TAB_SELECTED_COLOR_VAR = "TAB_SELECTED_COLOR";

static QString darkStylesheet;
static QString lightStylesheet;

static QString hiDensityFontStylesheet;
static QString loDensityFontStylesheet;

// We have two styles: one for the light-colored theme and one for
// the dark-colored theme.
//
// Each theme indicates the colors that are to be used for the foreground
// (mostly text) and the background.
//
// Even within a theme, not all widgets use the same colors. The style
// sheet accomodates this by associating colors based on "properties"
// that can be assigned to a widget. (Properties are listed in the .ui
// file.)
//
// So far, we have these special styles for each theme:
//   "MajorTab":       The area of the tab buttons on the left of the window
//   "MajorTabTitle":  Section titles separating the tab buttons
//   "Title":          Section titles in the main part of the window
//   "Tool":           Buttons whose text is the same color as a checkbox
//   "EditableValue":    Text that can be edited
//   "SliderLabel":      The label on a slider
//   "SmsBox":           The one item that has a border on all four sides
//   "GradientDivider":  The vertical line to the right of the main tabs
//   <Normal>:         Text in the main part of the window
//
// These are identified by the value of their "ColorGroup" or "class"
// property.

// These are the colors used in the two themes

QHash<QString, QString> lightValues {
    {"BOX_COLOR",            "#e0e0e0"},  // Boundary around SMS text area
    {"BKG_COLOR",            "#f0f0f0"},  // Main page background
    {"DISABLED_TOOL_COLOR",  "#baeae4"},  // Grayed-out button text
    {"DIVIDER_COLOR",        "#e0e0e0"},  // Line between items
    {"EDIT_COLOR",           "#e0e0e0"},  // Line under editable fields
    {"LARGE_DIVIDER_COLOR",  "#ebebeb"},  // Start of large divider's gradient
    {MAJOR_TAB_COLOR_VAR,    "#91a4ad"},  // Text of major tabs
    {"MAJOR_TITLE_COLOR",    "#617d8a"},  // Text of major tab separators
    {"SCROLL_BKG_COLOR",     "#f6f6f6"},  // Background of scroll bar
    {"SCROLL_HANDLE_COLOR",  "#d9d9d9"},  // Handle of scroller
    {TAB_BKG_COLOR_VAR,      "#ffffff"},  // Background of major tabs
    {TAB_SELECTED_COLOR_VAR, "#f5f5f5"},  // Background of the selected major tab
    {"TAB_DARKENED_COLOR",   "#e6e6e6"},
    {"TABLE_BOTTOM_COLOR",   "#e0e0e0"},
    {"TEXT_COLOR",           "#212121"},  // Main page text
    {"TITLE_COLOR",          "#757575"},  // Main page titles
    {"TOOL_COLOR",           "#00bea4"},  // Checkboxes, sliders, etc.
    {THEME_PATH_VAR,           "light"},  // Icon directory under images/
};

QHash<QString, QString> darkValues {
    {"BOX_COLOR",             "#414a50"},
    {"BKG_COLOR",             "#273238"},
    {"DISABLED_TOOL_COLOR",   "#1b5c58"},
    {"DIVIDER_COLOR",         "#e0e0e0"},
    {"EDIT_COLOR",            "#808080"},
    {"LARGE_DIVIDER_COLOR",   "#1f282d"},
    {MAJOR_TAB_COLOR_VAR,     "#bdc0c3"},
    {"MAJOR_TITLE_COLOR",     "#e5e6e7"},
    {"SCROLL_BKG_COLOR",      "#333b43"},
    {"SCROLL_HANDLE_COLOR",   "#1d272c"},
    {TAB_BKG_COLOR_VAR,       "#394249"},
    {TAB_SELECTED_COLOR_VAR,  "#313c42"},
    {"TAB_DARKENED_COLOR",    "#20292e"},
    {"TABLE_BOTTOM_COLOR",    "#1d272c"},
    {"TEXT_COLOR",            "#eeeeee"},
    {"TITLE_COLOR",           "#bec1c3"},
    {"TOOL_COLOR",            "#00bea4"},
    {THEME_PATH_VAR,             "dark"},
};

QHash<QString, QString> hiDensityValues {
    {"FONT_MEDIUM", "8pt"},
    {"FONT_LARGE", "10pt"},
};

QHash<QString, QString> loDensityValues {
    {"FONT_MEDIUM", "8pt"},
    {"FONT_LARGE", "10pt"},
};

// Encapsulates parsing a stylesheet template and generating a stylesheet
// from template.
// Stylesheet templates are arbitrary strings which may contain references
// to variables. A reference to a variable looks like this: %%variable_name%%
// The variable name is allowed to contain alphabetic characters.
class StylesheetTemplate {
    enum class TemplateBlockType { RawText, UnboundVariable };

    struct TemplateBlock {
        TemplateBlockType type;
        QString text;
    };

    // Helper function. Looks at the next character in the stream and discards
    // it if it is equal to a given character. Returns true if the next
    // character in the stream has been discarded, false if not.
    static bool peekAndDiscard(char discard_char, QIODevice* stream) {
        char c;
        if (stream->peek(&c, 1) > 0) {
            return c == discard_char ? stream->getChar(nullptr) : false;
        } else {
            return false;
        }
    }

public:
    // Loads the template from a given location and parses it.
    explicit StylesheetTemplate(const QString& location) : mOk(true) {
        QFile source_file(location);
        if (!source_file.open(QIODevice::ReadOnly)) {
            qWarning("StylesheetTemplate: could not open input file %s",
                     location.toStdString().c_str());
            mOk = false;
            return;
        }

        TemplateBlockType current_block_type = TemplateBlockType::RawText;
        char current_char;
        QString buf;
        while (mOk && source_file.getChar(&current_char)) {
            switch(current_block_type) {
            case TemplateBlockType::RawText:
                if (current_char == '%') {
                    if (peekAndDiscard('%', &source_file)) {
                        addBlock(current_block_type, buf);
                        current_block_type = TemplateBlockType::UnboundVariable;
                        buf.clear();
                    }
                } else {
                    buf.append(current_char);
                }
                break;

            case TemplateBlockType::UnboundVariable:
                if (isalpha(current_char) || current_char == '_') {
                    buf.append(current_char);
                } else {
                    if (peekAndDiscard('%', &source_file)) {
                        addBlock(current_block_type, buf);
                        current_block_type = TemplateBlockType::RawText;
                        buf.clear();
                    } else {
                        qWarning("StylesheetTemplate: Bad variable name %s",
                                 buf.toStdString().c_str());
                        buf.clear();
                        current_block_type = TemplateBlockType::RawText;
                        mOk = false;
                    }
                }
                break;
            }
        }

        // Handle the last remaining block.
        switch (current_block_type) {
        case TemplateBlockType::RawText:
            // flush.
            addBlock(current_block_type, buf);
            break;
        case TemplateBlockType::UnboundVariable:
            // This shouldn't happen for valid inputs.
            qWarning("StylesheetTemplate: Unterminated variable name %s",
                     buf.toStdString().c_str());
            mOk = false;
            break;
        }
    }

    // Outputs a stylesheet to the given |stream| with all the references to
    // variables substituted by values looked up from |arguments|.
    // Returns true if the operation is successful, false otherwise.
    bool render(const QHash<QString, QString>& arguments, QTextStream* stream) const {
        for (const auto& block : mBlocks) {
            switch(block.type) {
            case TemplateBlockType::RawText:
                (*stream) << block.text;
                break;
            case TemplateBlockType::UnboundVariable:
                auto value_iterator = arguments.find(block.text);
                if (value_iterator != arguments.end()) {
                    (*stream) << value_iterator.value();
                } else {
                    qWarning("StylesheetTemplate: variable %s unbound",
                             block.text.toStdString().c_str());
                    return false;
                }
                break;
            }
        }
        return true;
    }

    // Returns true if the template has been parsed successfully.
    bool isOk() const {
        return mOk;
    }

private:
    // Helper function - appends a new element to the list of blocks.
    void addBlock(TemplateBlockType type, const QString& str) {
        mBlocks.emplace_back();
        mBlocks.back().type = type;
        mBlocks.back().text = str;
    }

    std::vector<TemplateBlock> mBlocks;
    bool mOk;
};

static bool initializeStylesheets() {
    StylesheetTemplate tpl(":/styles/stylesheet_template.css");
    if (!tpl.isOk()) {
        qWarning("Failed to load stylesheet template!");
        return false;
    }

    QTextStream dark_stylesheet_stream(&darkStylesheet);
    if (!tpl.render(darkValues, &dark_stylesheet_stream)) {
        return false;
    }

    QTextStream light_stylesheet_stream(&lightStylesheet);
    if (!tpl.render(lightValues, &light_stylesheet_stream)) {
        return false;
    }

    StylesheetTemplate font_tpl(":/styles/fonts_stylesheet_template.css");
    if (!font_tpl.isOk()) {
        qWarning("Failed to load font stylesheet template!");
        return false;
    }

    QTextStream hi_font_stylesheet_stream(&hiDensityFontStylesheet);
    if (!font_tpl.render(hiDensityValues, &hi_font_stylesheet_stream)) {
        return false;
    }

    QTextStream lo_font_stylesheet_stream(&loDensityFontStylesheet);
    if (!font_tpl.render(loDensityValues, &lo_font_stylesheet_stream)) {
        return false;
    }

    return true;
}

const QString& stylesheetForTheme(SettingsTheme theme) {
    static std::atomic<bool> stylesheets_initialized(false);
    static QMutex init_mutex;
    if (!stylesheets_initialized) {
        init_mutex.lock();
        if (!stylesheets_initialized) {
            if (!initializeStylesheets()) {
                qWarning("Failed to initialize UI stylesheets!");
            }
        }
        stylesheets_initialized = true;
        init_mutex.unlock();
    }

    switch (theme) {
    case SETTINGS_THEME_DARK:
        return darkStylesheet;
    case SETTINGS_THEME_LIGHT:
        return lightStylesheet;
    default:
        return lightStylesheet;
    }
}

const QString& fontStylesheet(bool hi_density) {
    return hi_density ? hiDensityFontStylesheet : loDensityFontStylesheet;
}


const QHash<QString, QString>& stylesheetValues(SettingsTheme theme) {
    return theme == SETTINGS_THEME_LIGHT ? lightValues : darkValues;
}



}
