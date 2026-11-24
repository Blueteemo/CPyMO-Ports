#include <apgcli.h>
#include <apgtask.h>
#include <e32base.h>
#include <eikenv.h>
#include <f32file.h>
#include <utf.h>
#include <stdio.h>
#include <string.h>

namespace {
class CMovieSession : public CBase
{
public:
    explicit CMovieSession(CEikonEnv& env)
        : ws(env.WsSession()), ownGroup(env.RootWin().Identifier()),
          timer(NULL), error(KErrNone), finished(EFalse), playerGroup(0),
          launchPolls(0), returnPolls(0), restoredFocus(EFalse), backgroundSeen(EFalse) {}

    ~CMovieSession()
    {
        if (timer) timer->Cancel();
        delete timer;
    }

    void StartL(const TDesC& path)
    {
        if (ws.GetFocusWindowGroup() != ownGroup) User::Leave(KErrNotReady);
        timer = CPeriodic::NewL(CActive::EPriorityStandard);

        RApaLsSession appServer;
        User::LeaveIfError(appServer.Connect());
        CleanupClosePushL(appServer);
        User::LeaveIfError(appServer.StartDocument(path, playerThread, RApaLsSession::ESwitchFiles));
        CleanupStack::PopAndDestroy(&appServer);

        puts("[Video] system player launched.");
        fflush(stdout);
        timer->Start(100000, 100000, TCallBack(&CMovieSession::Poll, this));
    }

    void NoteBackground()
    {
        if (backgroundSeen) return;
        backgroundSeen = ETrue;
        puts("[Video] game in background.");
        fflush(stdout);
    }

    TBool Result(TInt& out) const
    {
        if (!finished) return EFalse;
        out = error;
        return ETrue;
    }

private:
    static TInt Poll(TAny* context)
    {
        CMovieSession& session = *static_cast<CMovieSession*>(context);
        if (!session.finished) session.CheckFocus();
        return 0;
    }

    TBool IsPlayerGroup(TInt group) const
    {
        TThreadId thread;
        return group > 0 &&
            ws.GetWindowGroupClientThreadId(group, thread) == KErrNone && thread == playerThread;
    }

    void CheckFocus()
    {
        const TInt group = ws.GetFocusWindowGroup();
        if (group == ownGroup) {
            if (backgroundSeen) {
                if (++returnPolls == 2) finished = ETrue;
            } else if (++launchPolls == 100) {
                error = KErrTimedOut;
                finished = ETrue;
            }
            return;
        }

        returnPolls = 0;
        if (group > 0) NoteBackground();
        if (IsPlayerGroup(group)) {
            playerGroup = group;
        } else if (!restoredFocus && playerGroup > 0 && !IsPlayerGroup(playerGroup)) {
            TApaTask task(ws);
            task.SetWgId(ownGroup);
            task.BringToForeground();
            restoredFocus = ETrue;
        }
    }

    RWsSession& ws;
    TInt ownGroup;
    CPeriodic* timer;
    TThreadId playerThread;
    TInt error;
    TBool finished;
    TInt playerGroup;
    TInt launchPolls;
    TInt returnPolls;
    TBool restoredFocus;
    TBool backgroundSeen;
};

CMovieSession* movieSession = NULL;

void OpenMovieL(const char* utf8Path)
{
    CEikonEnv* env = CEikonEnv::Static();
    if (!env || !CActiveScheduler::Current()) User::Leave(KErrNotReady);
    const TPtrC8 utf8(reinterpret_cast<const TUint8*>(utf8Path), strlen(utf8Path));
    TFileName path;
    if (CnvUtfConverter::ConvertToUnicodeFromUtf8(path, utf8) != 0)
        User::Leave(KErrBadName);
    for (TInt i = 0; i < path.Length(); ++i)
        if (path[i] == '/') path[i] = '\\';

    TEntry file;
    User::LeaveIfError(env->FsSession().Entry(path, file));
    if (file.IsDir()) User::Leave(KErrBadName);

    movieSession = new (ELeave) CMovieSession(*env);
    movieSession->StartL(path);
}
}

extern "C" void cpymo_s60_movie_close(void)
{
    delete movieSession;
    movieSession = NULL;
}

extern "C" int cpymo_s60_movie_start(const char* path)
{
    if (movieSession) return KErrInUse;
    TRAPD(error, OpenMovieL(path));
    if (error != KErrNone) cpymo_s60_movie_close();
    return error;
}

extern "C" int cpymo_s60_movie_result(int* status)
{
    return movieSession && movieSession->Result(*status);
}

extern "C" int cpymo_s60_movie_active(void)
{
    return movieSession != NULL;
}

extern "C" void cpymo_s60_movie_note_background(void)
{
    if (movieSession) movieSession->NoteBackground();
}
