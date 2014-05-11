/***************************************************************
 * Name:      ClangPlugin
 * Purpose:   Code::Blocks plugin
 * Author:     ()
 * Created:   2012-12-10
 * Copyright:
 * License:   GPL
 **************************************************************/

#ifndef CLANG_H_INCLUDED
#define CLANG_H_INCLUDED

// For compilers that support precompilation, includes <wx/wx.h>
#include <wx/wxprec.h>

#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <cbplugin.h>
#include <cbstyledtextctrl.h>

#include <clang-c/Index.h>

#include "ClangThread.h"

class Clang : public cbPlugin
{
public:
    /** Constructor. */
    Clang();
    /** Destructor. */
    virtual ~Clang();

    /** Invoke configuration dialog. */
    virtual int Configure();

    /** Return the plugin's configuration priority.
      * This is a number (default is 50) that is used to sort plugins
      * in configuration dialogs. Lower numbers mean the plugin's
      * configuration is put higher in the list.
      */
    virtual int GetConfigurationPriority() const { return 50; }

    /** Return the configuration group for this plugin. Default is cgUnknown.
      * Notice that you can logically OR more than one configuration groups,
      * so you could set it, for example, as "cgCompiler | cgContribPlugin".
      */
    virtual int GetConfigurationGroup() const { return cgUnknown; }

    /** Return plugin's configuration panel.
      * @param parent The parent window.
      * @return A pointer to the plugin's cbConfigurationPanel. It is deleted by the caller.
      */
    virtual cbConfigurationPanel* GetConfigurationPanel(wxWindow* parent){ return 0; }

    /** Return plugin's configuration panel for projects.
      * The panel returned from this function will be added in the project's
      * configuration dialog.
      * @param parent The parent window.
      * @param project The project that is being edited.
      * @return A pointer to the plugin's cbConfigurationPanel. It is deleted by the caller.
      */
    virtual cbConfigurationPanel* GetProjectConfigurationPanel(wxWindow* parent, cbProject* project){ return 0; }

    /** This method is called by Code::Blocks and is used by the plugin
      * to add any menu items it needs on Code::Blocks's menu bar.\n
      * It is a pure virtual method that needs to be implemented by all
      * plugins. If the plugin does not need to add items on the menu,
      * just do nothing ;)
      * @param menuBar the wxMenuBar to create items in
      */
    virtual void BuildMenu(wxMenuBar* menuBar){}

    /** This method is called by Code::Blocks core modules (EditorManager,
      * ProjectManager etc) and is used by the plugin to add any menu
      * items it needs in the module's popup menu. For example, when
      * the user right-clicks on a project file in the project tree,
      * ProjectManager prepares a popup menu to display with context
      * sensitive options for that file. Before it displays this popup
      * menu, it asks all attached plugins (by asking PluginManager to call
      * this method), if they need to add any entries
      * in that menu. This method is called.\n
      * If the plugin does not need to add items in the menu,
      * just do nothing ;)
      * @param type the module that's preparing a popup menu
      * @param menu pointer to the popup menu
      * @param data pointer to FileTreeData object (to access/modify the file tree)
      */
    virtual void BuildModuleMenu(const ModuleType type, wxMenu* menu, const FileTreeData* data = 0);

    /** This method is called by Code::Blocks and is used by the plugin
      * to add any toolbar items it needs on Code::Blocks's toolbar.\n
      * It is a pure virtual method that needs to be implemented by all
      * plugins. If the plugin does not need to add items on the toolbar,
      * just do nothing ;)
      * @param toolBar the wxToolBar to create items on
      * @return The plugin should return true if it needed the toolbar, false if not
      */
    virtual bool BuildToolBar(wxToolBar* toolBar){ return false; }
protected:

    class DiagnosticFixIt
    {
    public:
        DiagnosticFixIt() : start(0), end(0){}
        unsigned int start, end;
        wxString orig, replace;
    };

    class DiagnosticMessage
    {
    public:
        DiagnosticMessage() : start(0), end(0){}
        DiagnosticMessage(int start, int end, wxString message) : start(start), end(end), message(message) {}
        unsigned int start, end;
        wxString message;
        std::vector<DiagnosticFixIt> fixIts;
    };

    virtual void OnAttach();
    virtual void OnRelease(bool appShutDown);

    void OnEditorOpen(CodeBlocksEvent &event);
    void OnEditorSave(CodeBlocksEvent &event);
    void OnEditorActivated(CodeBlocksEvent &event);

    void OnEditorTooltip(CodeBlocksEvent &event);

    void OnProjectActivated(CodeBlocksEvent &event);

    void OnThreadParsed(wxCommandEvent &event);

    void ParseFile(const wxString &filename);

    void GetDiagnosticMessages(int pos, std::vector<DiagnosticMessage> &messages);

    wxString MakeCommandLine(const wxString &filename);

    void SetupIndicators(cbStyledTextCtrl *stc);
    void ClearIndicators(cbStyledTextCtrl *stc);

    void ClearTranslationUnits();

    wxString GetSourceFile(const wxString &filePath);

    CXIndex index;
    std::map<wxString, CXTranslationUnit> translationUnits;

    std::vector<DiagnosticMessage> messages;

    wxString currentFile;
    wxString prevCommandLine;

    wxString sysIncludePath;

    ClangThread *thread;
};

#endif // CLANG_H_INCLUDED
