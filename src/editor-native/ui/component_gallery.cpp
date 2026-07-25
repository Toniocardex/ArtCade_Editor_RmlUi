#include "editor-native/ui/component_gallery.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <string>
#include <vector>

namespace ArtCade::EditorNative {
namespace {

// A specimen: the markup under test plus the caption naming its state, so a
// diff points at "tool-btn / disabled" rather than at a pixel coordinate.
std::string specimen(const std::string& caption, const std::string& markup) {
    return "<div class=\"gallery-cell\">" + markup
         + "<div class=\"gallery-caption\">" + caption + "</div></div>";
}

// Wrapped, not two loose siblings: the sheet lays its sections out in columns,
// and a title has to travel with the row it names.
std::string section(const std::string& title, const std::string& cells) {
    return "<div class=\"gallery-section\">"
           "<div class=\"gallery-section-title\">" + title + "</div>"
           "<div class=\"gallery-row\">" + cells + "</div></div>";
}

/** A flat colour chip, for the roles that are surfaces rather than controls. */
std::string swatch(const std::string& caption, const std::string& cls) {
    return specimen(caption, "<div class=\"gallery-swatch " + cls + "\"></div>");
}

} // namespace

std::string componentGalleryMarkup() {
    std::string html = "<div class=\"gallery\">";

    // -- Surfaces: the elevation ramp, in order, so an inversion is obvious --
    html += section("Surfaces — elevation ramp",
        swatch("sunken", "sw-sunken") + swatch("base", "sw-base")
        + swatch("chrome", "sw-chrome") + swatch("raised", "sw-raised")
        + swatch("hover", "sw-hover") + swatch("selected", "sw-selected")
        + swatch("disabled", "sw-disabled"));

    // -- Text roles, each on raised, which is what most of them sit on --
    html += section("Text roles",
        specimen("strong", "<div class=\"gallery-text tx-strong\">Ag</div>")
        + specimen("primary", "<div class=\"gallery-text tx-primary\">Ag</div>")
        + specimen("secondary", "<div class=\"gallery-text tx-secondary\">Ag</div>")
        + specimen("tertiary", "<div class=\"gallery-text tx-tertiary\">Ag</div>")
        + specimen("disabled", "<div class=\"gallery-text tx-disabled\">Ag</div>")
        + specimen("danger", "<div class=\"gallery-text tx-danger\">Ag</div>")
        + specimen("warning", "<div class=\"gallery-text tx-warning\">Ag</div>")
        + specimen("success", "<div class=\"gallery-text tx-success\">Ag</div>"));

    // -- tool-btn: the toolbar contract, including the state that regressed --
    html += section("tool-btn",
        specimen("normal", "<button class=\"tool-btn\">Grid</button>")
        + specimen("hover", "<button class=\"tool-btn\" data-force=\"hover\">Grid</button>")
        + specimen("active", "<button class=\"tool-btn active\">Grid</button>")
        + specimen("primary", "<button class=\"tool-btn primary\">Play</button>")
        + specimen("disabled", "<button class=\"tool-btn disabled\">Grid</button>"));

    // -- panel-btn: 65 uses across 12 files, the widest-shared control --
    html += section("panel-btn",
        specimen("normal", "<button class=\"panel-btn\">Add Layer</button>")
        + specimen("hover", "<button class=\"panel-btn\" data-force=\"hover\">Add Layer</button>")
        + specimen("active", "<button class=\"panel-btn active\">Add Layer</button>")
        + specimen("primary", "<button class=\"panel-btn primary\">Create Scene</button>")
        + specimen("disabled", "<button class=\"panel-btn disabled\">Add Layer</button>")
        // A disabled primary used to keep its accent border and white label —
        // half-enabled. Pinned here so it cannot drift back.
        + specimen("primary disabled",
                   "<button class=\"panel-btn primary disabled\">Create Scene</button>"));

    // -- menu-entry: the class whose contract broke in 805d596. Both the bare
    //    text form and the .with-shortcut form must render their label. --
    html += section("menu-entry",
        specimen("bare label", "<div class=\"gallery-menu\"><div class=\"menu-entry\">"
                               "<span class=\"icon\">&#xeb0b;</span>New</div></div>")
        + specimen("hover", "<div class=\"gallery-menu\"><div class=\"menu-entry\" "
                            "data-force=\"hover\"><span class=\"icon\">&#xeb0b;</span>New</div></div>")
        + specimen("active", "<div class=\"gallery-menu\"><div class=\"menu-entry active\">"
                             "<span class=\"icon\">&#xea3b;</span>Show Grid</div></div>")
        + specimen("disabled", "<div class=\"gallery-menu\"><div class=\"menu-entry disabled\">"
                               "<span class=\"icon\">&#xeb77;</span>Undo</div></div>")
        + specimen("with-shortcut", "<div class=\"gallery-menu\"><div class=\"menu-entry with-shortcut\">"
                                    "<span class=\"menu-entry-label\">Keyboard Shortcuts</span>"
                                    "<span class=\"menu-entry-shortcut\">F1</span></div></div>")
        + specimen("section + hint", "<div class=\"gallery-menu\">"
                                     "<div class=\"menu-section\">Object types</div>"
                                     "<div class=\"menu-entry\">Player"
                                     "<span class=\"menu-entry-hint\">object-1</span></div></div>"));

    // -- context-entry, including the submenu flyout that regressed twice --
    html += section("context-entry",
        specimen("normal", "<div class=\"gallery-menu\"><div class=\"context-entry\">"
                           "<span class=\"icon\">&#xeb0a;</span><span>Rename</span>"
                           "<span class=\"context-shortcut\">F2</span></div></div>")
        + specimen("hover", "<div class=\"gallery-menu\"><div class=\"context-entry\" "
                            "data-force=\"hover\"><span class=\"icon\">&#xeb0a;</span>"
                            "<span>Rename</span></div></div>")
        + specimen("destructive", "<div class=\"gallery-menu\"><div class=\"context-entry destructive\">"
                                  "<span class=\"icon\">&#xeb41;</span><span>Delete</span>"
                                  "<span class=\"context-shortcut\">Del</span></div></div>")
        + specimen("has-submenu", "<div class=\"gallery-menu\"><div class=\"context-entry has-submenu\">"
                                  "<span class=\"icon\">&#xeb2d;</span><span>Move to Layer</span>"
                                  "<span class=\"context-submenu-caret\">&#xea61;</span></div></div>"));

    // -- drop-entry: the value pickers --
    html += section("drop-entry",
        specimen("normal", "<div class=\"gallery-menu drop-list\">"
                           "<div class=\"drop-entry\">Layer 1</div></div>")
        + specimen("hover", "<div class=\"gallery-menu drop-list\">"
                            "<div class=\"drop-entry\" data-force=\"hover\">Layer 1</div></div>")
        + specimen("selected", "<div class=\"gallery-menu drop-list\">"
                               "<div class=\"drop-entry selected\">"
                               "<span class=\"drop-mark\">&#x25cf;</span> Layer 1</div></div>")
        + specimen("disabled", "<div class=\"gallery-menu drop-list\">"
                               "<div class=\"drop-entry disabled\">Layer 1</div></div>"));

    // -- logic-key-binding: the Logic Board key picker. Its hover rule was
    //    left dangling by the phase-2 migration and merged with the search
    //    panel's layout, so hovering the button made it full-width and shoved
    //    the card down. Pinned here in both states. --
    html += section("logic-key-binding",
        specimen("normal", "<div class=\"logic-key-binding\">"
                           "<button class=\"logic-key-capture\">Space</button>"
                           "<button class=\"logic-key-search-toggle\">Search key…</button></div>")
        + specimen("capture hover", "<div class=\"logic-key-binding\">"
                                    "<button class=\"logic-key-capture\" data-force=\"hover\">Space</button>"
                                    "<button class=\"logic-key-search-toggle\">Search key…</button></div>")
        + specimen("toggle hover", "<div class=\"logic-key-binding\">"
                                   "<button class=\"logic-key-capture\">Space</button>"
                                   "<button class=\"logic-key-search-toggle\" data-force=\"hover\">Search key…</button></div>")
        + specimen("capturing", "<div class=\"logic-key-binding\">"
                                "<button class=\"logic-key-capture capturing\">Press a key…</button></div>"));

    // -- prop-input: the Inspector field, sunken surface + focus ring --
    html += section("prop-input",
        specimen("normal", "<input type=\"text\" class=\"prop-input\" value=\"256\"/>")
        + specimen("hover", "<input type=\"text\" class=\"prop-input\" data-force=\"hover\" value=\"256\"/>")
        + specimen("focus", "<input type=\"text\" class=\"prop-input\" data-force=\"focus\" value=\"256\"/>")
        + specimen("disabled", "<input type=\"text\" class=\"prop-input\" disabled=\"disabled\" value=\"256\"/>"));

    // -- logic-value-input: the same field inside a rule card. It shipped for
    //    a long time with no fill and no border, so a Position value read as
    //    static text. Pinned so it cannot fall back to invisible. --
    html += section("logic-value-input",
        specimen("normal", "<input type=\"text\" class=\"logic-value-input\" value=\"64\"/>")
        + specimen("hover", "<input type=\"text\" class=\"logic-value-input\" data-force=\"hover\" value=\"64\"/>")
        + specimen("focus", "<input type=\"text\" class=\"logic-value-input\" data-force=\"focus\" value=\"64\"/>")
        + specimen("disabled", "<input type=\"text\" class=\"logic-value-input\" disabled=\"disabled\" value=\"64\"/>"));

    // -- logic-toggle: the boolean value control. Both options must stay
    //    visible and the selected one must be distinguishable — the single
    //    button it replaced printed one label and looked inert. --
    html += section("logic-toggle",
        specimen("on", "<div class=\"logic-toggle\">"
                       "<button class=\"logic-toggle-option selected\">On</button>"
                       "<button class=\"logic-toggle-option\">Off</button></div>")
        + specimen("off", "<div class=\"logic-toggle\">"
                          "<button class=\"logic-toggle-option\">On</button>"
                          "<button class=\"logic-toggle-option selected\">Off</button></div>")
        + specimen("hover", "<div class=\"logic-toggle\">"
                            "<button class=\"logic-toggle-option selected\">On</button>"
                            "<button class=\"logic-toggle-option\" data-force=\"hover\">Off</button></div>")
        + specimen("disabled", "<div class=\"logic-toggle\">"
                               "<button class=\"logic-toggle-option selected disabled\">On</button>"
                               "<button class=\"logic-toggle-option disabled\">Off</button></div>"));

    html += "</div>";
    return html;
}

void applyGalleryForcedStates(Rml::ElementDocument* document) {
    if (!document) return;
    // Depth-first walk: the gallery is small, and this avoids depending on a
    // query API whose availability varies across RmlUi versions.
    std::vector<Rml::Element*> stack{document};
    while (!stack.empty()) {
        Rml::Element* el = stack.back();
        stack.pop_back();
        const Rml::String forced = el->GetAttribute<Rml::String>("data-force", "");
        if (!forced.empty()) el->SetPseudoClass(forced, true);
        for (int i = 0; i < el->GetNumChildren(); ++i) stack.push_back(el->GetChild(i));
    }
}

} // namespace ArtCade::EditorNative
