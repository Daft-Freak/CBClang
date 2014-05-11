//10 dec 2012
#include <sdk.h> // Code::Blocks SDK
#include <configurationpanel.h>
#include <logmanager.h>
#include <projectmanager.h>
#include <cbproject.h>
#include <cbstyledtextctrl.h>
#include <compilerfactory.h>
#include <compilercommandgenerator.h>
#include <editormanager.h>
#include <cbeditor.h>

#include <wx/tokenzr.h>
#include <wx/dir.h>

#include "Clang.h"

#define ERROR_INDICATOR 15
#define WARNING_INDICATOR 16

// Register the plugin with Code::Blocks.
// We are using an anonymous namespace so we don't litter the global one.
namespace
{
    PluginRegistrant<Clang> reg(_T("Clang"));
}

Clang::Clang()
{
    // Make sure our resources are available.
    // In the generated boilerplate code we have no resources but when
    // we add some, it will be nice that this code is in place already ;)
    if(!Manager::LoadResource(_T("Clang.zip")))
    {
        NotifyMissingFile(_T("Clang.zip"));
    }
}

Clang::~Clang()
{
}

void Clang::OnAttach()
{
    index = clang_createIndex(0, 0);

    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_OPEN, new cbEventFunctor<Clang, CodeBlocksEvent>(this, &Clang::OnEditorOpen));
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_SAVE, new cbEventFunctor<Clang, CodeBlocksEvent>(this, &Clang::OnEditorSave));
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_ACTIVATED, new cbEventFunctor<Clang, CodeBlocksEvent>(this, &Clang::OnEditorActivated));

    Manager::Get()->RegisterEventSink(cbEVT_PROJECT_ACTIVATE, new cbEventFunctor<Clang, CodeBlocksEvent>(this, &Clang::OnProjectActivated));

    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_TOOLTIP, new cbEventFunctor<Clang, CodeBlocksEvent>(this, &Clang::OnEditorTooltip));
    //Manager::Get()->GetLogManager()->Log( _("Hello World!") );
    Connect(wxID_ANY, EVT_COMMAND_THREAD_PARSED, (wxObjectEventFunction)(wxEventFunction)(wxCommandEventFunction)&Clang::OnThreadParsed, NULL, this);

    //set up indicator style
    cbEditor *editor = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();

    if(editor)
    {
        cbStyledTextCtrl *stc = editor->GetControl();

        SetupIndicators(stc);
    }

    //make sure libclang can actually find it's includes
    CXUnsavedFile file;
    file.Filename = "testinc.c";
    file.Contents = "#include <limits.h>";
    file.Length = strlen(file.Contents);

    CXTranslationUnit tmp = clang_parseTranslationUnit(index, "testinc.c", nullptr, 0, &file, 1, 0);

    if(clang_getNumDiagnostics(tmp) != 0)
    {
        Manager::Get()->GetLogManager()->Log(_("libclang can't find includes, helping it a bit..."));

        if(wxDir::Exists(wxT("/usr/lib/clang/")))
        {
            wxDir dir(wxT("/usr/lib/clang/"));

            wxString filename;
            bool cont = dir.GetFirst(&filename, wxEmptyString, wxDIR_DIRS);
            while(cont)
            {
                wxString fullDir = wxT("/usr/lib/clang/") + filename + wxT("/include");
                if(wxDir::Exists(fullDir))
                {
                    clang_disposeTranslationUnit(tmp);
                    const char *args[1] = {(wxT("-I") + fullDir).mb_str()};

                    tmp = clang_parseTranslationUnit(index, "testinc.c", args, 1, &file, 1, 0);
                    if(clang_getNumDiagnostics(tmp))
                    {
                        Manager::Get()->GetLogManager()->Log(_("\tFound ") + fullDir);
                        sysIncludePath = fullDir;
                        break;
                    }
                }
                cont = dir.GetNext(&filename);
            }
        }

        if(sysIncludePath.empty())
            Manager::Get()->GetLogManager()->LogWarning(_("\tCouldn't find a system include path for libclang!"));
    }

    clang_disposeTranslationUnit(tmp);

    //start the thread
    thread = new ClangThread(this);
    thread->SetIndex(index);
    thread->Create();
    thread->Run();

}

void Clang::OnRelease(bool appShutDown)
{
    thread->Stop();
    thread->Delete();

    ClearTranslationUnits();

    clang_disposeIndex(index);
}

int Clang::Configure()
{
    //create and display the configuration dialog for your plugin
    cbConfigurationDialog dlg(Manager::Get()->GetAppWindow(), wxID_ANY, _("Your dialog title"));
    cbConfigurationPanel* panel = GetConfigurationPanel(&dlg);
    if (panel)
    {
        dlg.AttachConfigurationPanel(panel);
        PlaceWindow(&dlg);
        return dlg.ShowModal() == wxID_OK ? 0 : -1;
    }
    return -1;
}


void Clang::BuildModuleMenu(const ModuleType type, wxMenu* menu, const FileTreeData* data)
{
    NotImplemented(_T("Clang::BuildModuleMenu()"));
}

void Clang::OnEditorOpen(CodeBlocksEvent &event)
{
    /*if(IsAttached())
    {
        EditorBase *editor = event.GetEditor();

        ScanFile(editor->GetFilename());
    }*/

    event.Skip();
}

void Clang::OnEditorSave(CodeBlocksEvent &event)
{
    if(IsAttached())
    {
        EditorBase *editor = event.GetEditor();

        ParseFile(editor->GetFilename());
    }

    event.Skip();
}

void Clang::OnEditorActivated(CodeBlocksEvent &event)
{
    if(IsAttached() && !ProjectManager::IsBusy())
    {
        Manager::Get()->GetLogManager()->Log( _("!! Activate !!"));
        EditorBase *editor = event.GetEditor();

        if(editor->GetFilename().compare(currentFile) != 0)
        {
            ParseFile(editor->GetFilename());
            currentFile = editor->GetFilename();
        }
    }
    event.Skip();
}

void Clang::OnEditorTooltip(CodeBlocksEvent &event)
{
    if(IsAttached())
    {
        EditorBase *base = event.GetEditor();
        cbEditor *editor = nullptr;
        cbStyledTextCtrl *stc = nullptr;

        if(base && base->IsBuiltinEditor())
        {
            editor = static_cast<cbEditor *>(base);
            stc = editor->GetControl();
        }

        if(!editor || editor->IsContextMenuOpened())
        {
            event.Skip();
            return;
        }

        //get pos
        int pos = stc->PositionFromPointClose(event.GetX(), event.GetY());
        if(pos < 0 || pos >= stc->GetLength())
        {
            event.Skip();
            return;
        }

        std::vector<DiagnosticMessage> messages;

        GetDiagnosticMessages(pos, messages);

        if(!messages.empty())
        {
            wxString message;
            for(auto it = messages.begin(); it != messages.end(); ++it)
            {
                if(it != messages.begin())
                    message << wxT("\n");
                message << it->message;
            }

            if(editor->GetControl()->CallTipActive())
                editor->GetControl()->CallTipCancel();

            stc->CallTipShow(pos, message);

            event.SetExtraLong(1);
        }
        //else
            event.Skip();
    }
}

void Clang::OnProjectActivated(CodeBlocksEvent &event)
{
    Manager::Get()->GetLogManager()->Log(_("Prj activate"));
    cbProject *project = event.GetProject();
    //parse entire project
    for(int i = 0; i < project->GetFilesCount(); i++)
    {
        //ParseFile(project->GetFile(i)->file.GetFullPath());
    }
}

void Clang::OnThreadParsed(wxCommandEvent &event)
{
    CXTranslationUnit unit = reinterpret_cast<CXTranslationUnit>(event.GetClientData());

    if(!unit)
        return;

    CXString name = clang_getTranslationUnitSpelling(unit);
    wxString filePath(clang_getCString(name), wxConvUTF8);
    clang_disposeString(name);

    Manager::Get()->GetLogManager()->Log( _("Parsed ") + filePath);

    if(translationUnits.find(filePath) != translationUnits.end() && translationUnits[filePath] != unit)
    {
        Manager::Get()->GetLogManager()->Log(filePath +  _(" has duplicated"));
    }

    translationUnits[filePath] = unit;


    if(filePath.compare(currentFile) == 0 || filePath.compare(GetSourceFile(currentFile)) == 0)
    {
        //wxMutexLocker lock(thread->GetMutex());

        cbEditor *editor = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
        if(editor)
        {
            cbStyledTextCtrl *stc = editor->GetControl();

            messages.clear();
            SetupIndicators(stc);
            ClearIndicators(stc);

            //diagnostics
            int numDiagnostics = clang_getNumDiagnostics(unit);

            Manager::Get()->GetLogManager()->Log(wxString::Format(_("%i diagnostics"), numDiagnostics));

            for(int i = 0; i < numDiagnostics; i++)
            {
                CXDiagnostic diag = clang_getDiagnostic(unit, i);
                CXString text = clang_getDiagnosticSpelling(diag);
                wxString message = wxString(clang_getCString(text), wxConvUTF8);

                if(stc)
                {
                    CXSourceLocation loc = clang_getDiagnosticLocation(diag);
                    unsigned int offset;
                    CXFile file;
                    clang_getSpellingLocation(loc, &file, nullptr, nullptr, &offset);
                    CXString diagFilename = clang_getFileName(file);

                    //check file name
                    if(currentFile.compare(wxString(clang_getCString(diagFilename), wxConvUTF8)) == 0)
                    {
                        DiagnosticMessage diagMessage;
                        diagMessage.message = message;

                        Manager::Get()->GetLogManager()->Log(message);

                        //set indicator for severity level
                        int indicatorId = ERROR_INDICATOR;

                        CXDiagnosticSeverity severity = clang_getDiagnosticSeverity(diag);

                        if(severity == CXDiagnostic_Warning)
                            indicatorId = WARNING_INDICATOR;

                        stc->SetIndicatorCurrent(indicatorId);


                        //fix-its
                        int numFixIts = clang_getDiagnosticNumFixIts(diag);

                        Manager::Get()->GetLogManager()->Log(wxString::Format(_("%i fix-its"), numFixIts));

                        for(int f = 0; f < numFixIts; f++)
                        {
                            DiagnosticFixIt diagFixIt;
                            CXSourceRange range;
                            CXString fixIt = clang_getDiagnosticFixIt(diag, f, &range);

                            CXSourceLocation start = clang_getRangeStart(range);
                            CXSourceLocation end = clang_getRangeEnd(range);

                            //get offsets
                            clang_getSpellingLocation(start, nullptr, nullptr, nullptr, &diagFixIt.start);
                            clang_getSpellingLocation(end, nullptr, nullptr, nullptr, &diagFixIt.end);

                            diagFixIt.orig = stc->GetTextRange(diagFixIt.start, diagFixIt.end);
                            diagFixIt.replace = wxString(clang_getCString(fixIt), wxConvUTF8);

                            clang_disposeString(fixIt);

                            Manager::Get()->GetLogManager()->Log(_("\t Replace \"") + diagFixIt.orig + _("\" with \"") + diagFixIt.replace + _("\""));

                            diagMessage.fixIts.push_back(diagFixIt);
                        }

                        //ranges
                        int numRanges = clang_getDiagnosticNumRanges(diag);
                        int usedRanges = 0;
                        for(int r = 0; r < numRanges; r++)
                        {
                            CXSourceRange range = clang_getDiagnosticRange(diag, r);
                            CXSourceLocation start = clang_getRangeStart(range);
                            CXSourceLocation end = clang_getRangeEnd(range);

                            //get offsets
                            unsigned int startOffset, endOffset;
                            CXFile sFile, eFile;
                            clang_getSpellingLocation(start, &sFile, nullptr, nullptr, &startOffset);
                            clang_getSpellingLocation(end, &eFile, nullptr, nullptr, &endOffset);

                            CXString sFilename = clang_getFileName(sFile);
                            CXString eFilename = clang_getFileName(eFile);

                            wxString startFileStr(clang_getCString(sFilename), wxConvUTF8);
                            wxString endFileStr(clang_getCString(eFilename), wxConvUTF8);

                            clang_disposeString(sFilename);
                            clang_disposeString(eFilename);

                            //start and end in different files
                            if(startFileStr.compare(endFileStr) != 0 || startFileStr.compare(currentFile) != 0 || endFileStr.compare(currentFile) != 0)
                            {
                                continue;
                            }

                            stc->IndicatorFillRange(startOffset, endOffset - startOffset);

                            diagMessage.start = startOffset;
                            diagMessage.end = endOffset;

                            Manager::Get()->GetLogManager()->Log(wxString::Format(_("Range: %i - %i"), diagMessage.start, diagMessage.end));

                            messages.push_back(diagMessage);
                            usedRanges++;
                        }

                        //no ranges found
                        if(!usedRanges)
                        {
                            diagMessage.start = offset;
                            diagMessage.end =  offset + 1;
                            stc->IndicatorFillRange(offset, 1);

                            messages.push_back(diagMessage);
                        }
                    }
                    else
                    {
                        wxString tmpFilename = wxString(clang_getCString(diagFilename), wxConvUTF8);
                        Manager::Get()->GetLogManager()->Log(wxString::Format(_("\t... diag %i(%s) is for %s, not current file (%s)"), i, message.c_str(), tmpFilename.c_str(), currentFile.c_str()));
                    }

                    clang_disposeString(diagFilename);
                }

                clang_disposeDiagnostic(diag);
                clang_disposeString(text);
            }
        }
    }
    else
        Manager::Get()->GetLogManager()->Log(wxString::Format(_("\t... is not current file (%s)"), currentFile.c_str()));

    /*CXTUResourceUsage resUsage = clang_getCXTUResourceUsage(unit);
    wxString stuff;
    for(int i = 0; i < resUsage.numEntries; i++)
    {
        stuff << wxString(clang_getTUResourceUsageName(resUsage.entries[i].kind), wxConvUTF8) << wxT(":") << resUsage.entries[i].amount << wxT(" ");
    }

    clang_disposeCXTUResourceUsage(resUsage);

    Manager::Get()->GetLogManager()->Log(stuff);*/
}

void Clang::ParseFile(const wxString &filename)
{
    if(filename.empty())
        return;

    wxString ext = wxFileName(filename).GetExt().Lower();

    if(ext.compare(wxT("cpp")) != 0 && ext.compare(wxT("cxx")) != 0 && ext.compare(wxT("c")) != 0 && ext.compare(wxT("h")) != 0)
    {
        return;
    }

    cbEditor *editor = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    cbStyledTextCtrl *stc = nullptr;
    if(editor)
        cbStyledTextCtrl *stc = editor->GetControl();

    //file change
    if(currentFile.compare(filename) != 0)
    {
        if(stc)
        {
            messages.clear();
            ClearIndicators(stc);
        }
    }

    wxString fileToUse = filename;

    //use source file instead if this is a header
    wxString sourceFile = GetSourceFile(filename);

    if(sourceFile.compare(filename) != 0)
    {
        Manager::Get()->GetLogManager()->Log(_("Using source file: ") + sourceFile + _(", instead of: ") + filename);
        fileToUse = sourceFile;
    }

    //get command line
    wxString commandLine = MakeCommandLine(fileToUse);

    //check for compiler options changing(badly)
    if(!prevCommandLine.empty() && prevCommandLine.compare(commandLine) != 0)
    {
        Manager::Get()->GetLogManager()->Log( _("Compiler options changed"));
        ClearTranslationUnits();
    }
    prevCommandLine = commandLine;

    auto unitIt = translationUnits.find(fileToUse);
    if(unitIt != translationUnits.end())
    {
        Manager::Get()->GetLogManager()->Log( _("Adding ") + fileToUse + _(" to reparse list"));
        thread->AddFileReparse(unitIt->second);
    }
    else
    {
        Manager::Get()->GetLogManager()->Log( _("Adding ") + fileToUse + _(" to parse list"));

        //Manager::Get()->GetLogManager()->Log(_("Command line: ") + commandLine);

        thread->AddFile(fileToUse, commandLine);
    }
}

void Clang::GetDiagnosticMessages(int pos, std::vector<DiagnosticMessage> &messages)
{
    for(auto it = this->messages.begin(); it != this->messages.end(); ++it)
    {
        if(pos >= it->start && pos <= it->end)
        {
            messages.push_back(*it);
        }
    }
}

wxString Clang::MakeCommandLine(const wxString &filename)
{
    cbProject *project = Manager::Get()->GetProjectManager()->GetActiveProject();

    if(!project)
        return wxT("");

    ProjectFile *file = project->GetFileByFilename(filename, false);

    if(!file)
        return wxT("");

    //not in target
    if(file->buildTargets.Index(project->GetActiveBuildTarget()) == wxNOT_FOUND)
        return wxT("");

    ProjectBuildTarget *target = project->GetBuildTarget(project->GetActiveBuildTarget());
    if(!target)
        return wxT("");
    wxString compilerID = target->GetCompilerID();

    Compiler *comp = CompilerFactory::GetCompiler(compilerID);

    if(!comp)
        return wxT("");

    const pfDetails& pfd = file->GetFileDetails(target);

    wxString objectFile = (comp->GetSwitches().UseFlatObjects) ? pfd.object_file_flat : pfd.object_file;

    const CompilerTool &tool = comp->GetCompilerTool(ctCompileObjectCmd, _(".") + file->file.GetExt());

    wxString command = wxT("$options $includes");
    comp->GetCommandGenerator(project)->GenerateCommandLine(command, target, file, UnixFilename(pfd.source_file_absolute_native), objectFile, pfd.object_file_flat, pfd.dep_file);

    if(!sysIncludePath.empty())
        command += wxT(" -I") + sysIncludePath;

    return command;
}

void Clang::SetupIndicators(cbStyledTextCtrl *stc)
{
    stc->IndicatorSetStyle(ERROR_INDICATOR, wxSCI_INDIC_ROUNDBOX);
    stc->IndicatorSetForeground(ERROR_INDICATOR, wxColour(255, 0, 0));
    stc->IndicatorSetAlpha(ERROR_INDICATOR, 100);
    stc->IndicatorSetOutlineAlpha(ERROR_INDICATOR, 200);

    stc->IndicatorSetStyle(WARNING_INDICATOR, wxSCI_INDIC_ROUNDBOX);
    stc->IndicatorSetForeground(WARNING_INDICATOR, wxColour(255, 255, 0));
    stc->IndicatorSetAlpha(WARNING_INDICATOR, 100);
    stc->IndicatorSetOutlineAlpha(WARNING_INDICATOR, 200);
}

void Clang::ClearIndicators(cbStyledTextCtrl *stc)
{
    stc->SetIndicatorCurrent(ERROR_INDICATOR);
    stc->IndicatorClearRange(0, stc->GetLength());

    stc->SetIndicatorCurrent(WARNING_INDICATOR);
    stc->IndicatorClearRange(0, stc->GetLength());
}

void Clang::ClearTranslationUnits()
{
    for(auto it = translationUnits.begin(); it != translationUnits.end(); ++it)
    {
        clang_disposeTranslationUnit(it->second);
    }
    translationUnits.clear();
}

// check if this file is a header and get the matching source file if it is
wxString Clang::GetSourceFile(const wxString &filePath)
{
    wxFileName filename(filePath);
    wxString ext = filename.GetExt().Lower();

    std::vector<wxString> sourceExts = {wxT("c"), wxT("cpp"), wxT("cxx")};

    if(ext.compare(wxT("h")) == 0)
    {
        for(auto &ext : sourceExts)
        {
            filename.SetExt(ext);
            if(filename.IsFileReadable())
                return filename.GetFullPath();

            filename.SetExt(ext.Upper());
            if(filename.IsFileReadable())
                return filename.GetFullPath();
        }
    }

    return filePath;
}
