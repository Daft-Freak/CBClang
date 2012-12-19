#ifndef CLANGTHREAD_H
#define CLANGTHREAD_H

#include <list>

#include <wx/thread.h>
#include <wx/event.h>

#include <clang-c/Index.h>

//wxDECLARE_EVENT(EVT_COMMAND_THREAD_PARSED, wxThreadEvent);
DECLARE_EVENT_TYPE(EVT_COMMAND_THREAD_PARSED, wxID_ANY);

class ClangThread : public wxThread
{
public:
    ClangThread(wxEvtHandler *parent);
    virtual ~ClangThread();

    void Stop();

    void SetIndex(CXIndex index){this->index = index;}

    void AddFile(const wxString &file, const wxString &commandLine);
    void AddFileReparse(CXTranslationUnit unit);

    wxMutex &GetMutex() {return clangMutex;}
protected:

    virtual ExitCode Entry();

    std::list<wxString> fileQueue;
    std::list<wxString> commandLines;
    std::list<CXTranslationUnit> reparseQueue;

    wxMutex queueMutex, clangMutex;
    wxCondition condition;

    wxEvtHandler *parent;
    CXIndex index;
    bool stop;
};

#endif // CLANGTHREAD_H
