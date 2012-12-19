#include "ClangThread.h"

#include <wx/tokenzr.h>

//wxDEFINE_EVENT(EVT_COMMAND_THREAD_PARSED, wxThreadEvent);
DEFINE_EVENT_TYPE(EVT_COMMAND_THREAD_PARSED);

ClangThread::ClangThread(wxEvtHandler *parent) : wxThread(wxTHREAD_DETACHED), parent(parent), condition(queueMutex), index(nullptr), stop(false)
{
}

ClangThread::~ClangThread()
{
}

void ClangThread::Stop()
{
    wxMutexLocker lock(queueMutex);
    stop = true;
    condition.Signal();
}

void ClangThread::AddFile(const wxString &file, const wxString &commandLine)
{
    wxMutexLocker lock(queueMutex);

    for(auto it = fileQueue.begin(); it != fileQueue.end(); ++it)
    {
        if(it->compare(file) == 0)
            return; //already in list
    }

    fileQueue.push_back(file);
    commandLines.push_back(commandLine);
    condition.Signal();
}

void ClangThread::AddFileReparse(CXTranslationUnit unit)
{
    wxMutexLocker lock(queueMutex);

    for(auto it = reparseQueue.begin(); it != reparseQueue.end(); ++it)
    {
        if(*it == unit)
            return; //already in list
    }
    reparseQueue.push_back(unit);

    condition.Signal();
}

wxThread::ExitCode ClangThread::Entry()
{
    while(!TestDestroy())
    {
        queueMutex.Lock();
        if(fileQueue.empty() && reparseQueue.empty())
        {
            //wait for work
            if(!stop)
                condition.Wait();
        }

        if(!fileQueue.empty())
        {
            wxString file = fileQueue.back();
            fileQueue.pop_back();

            wxString commandLine = commandLines.back();
            commandLines.pop_back();
            queueMutex.Unlock();

            clangMutex.Lock();

            wxStringTokenizer tok(commandLine);
            int numArgs = tok.CountTokens();
            char **args = new char *[numArgs];
            int i = 0;
            while(tok.HasMoreTokens())
            {
                wxString token = tok.GetNextToken();
                args[i] = new char[token.length() + 1];
                memcpy(args[i], token.mb_str(), token.length() + 1);
                i++;
            }

            CXTranslationUnit unit = clang_parseTranslationUnit(index, file.mb_str(), args, numArgs, nullptr, 0, 0);

            for(int i = 0; i < numArgs; i++)
                delete[] args[i];

            delete[] args;

            clangMutex.Unlock();

            //send parsed unit back
            wxCommandEvent event(EVT_COMMAND_THREAD_PARSED);
            event.SetClientData(unit);
            wxPostEvent(parent, event);
        }
        else if(!reparseQueue.empty())
        {
            CXTranslationUnit unit = reparseQueue.back();
            //reparseQueue.pop_back();
            auto it = --reparseQueue.end();
            queueMutex.Unlock();

            clangMutex.Lock();
            clang_reparseTranslationUnit(unit, 0, nullptr, clang_defaultReparseOptions(unit));
            clangMutex.Unlock();


            //send parsed unit back
            wxCommandEvent event(EVT_COMMAND_THREAD_PARSED);
            event.SetClientData(unit);
            wxPostEvent(parent, event);

            queueMutex.Lock();
            reparseQueue.erase(it);
            queueMutex.Unlock();
        }
        else
        {
            queueMutex.Unlock();
        }
    }

    return static_cast<wxThread::ExitCode>(0);
}
