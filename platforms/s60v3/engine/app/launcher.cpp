// CPyMO S60v3 independent application, preserving original PyMO games.
#include <e32base.h>
#include <eikapp.h>
#include <eikstart.h>
#include <sdlapp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <f32file.h>
#include <eikenv.h>
#include <eikappui.h>
#include <eikbtgpc.h>
#include <uikon.hrh>
#include <bautils.h>
#include <aknquerydialog.h>
#include <cpymo_font_trial.rsg>
#include "../source/cpymo-backends/sdl1/cpymo_s60_diagnostics.h"
extern "C" int SDL_main(int argc, char **argv);

static TBool native_query_open;
static int diagnostics_enabled;
static char log_buffer[4096];

extern "C" int cpymo_s60_diagnostics_enabled(void)
{
    return diagnostics_enabled;
}

extern "C" int cpymo_s60_native_query_open()
{
    return native_query_open;
}

class CLegacyMoQuery : public CAknQueryDialog
{
public:
    CLegacyMoQuery(const TDesC& continueText, const TDesC& backText)
        : CAknQueryDialog(ENoTone), continueLabel(continueText), backLabel(backText) {}

private:
    void PostLayoutDynInitL()
    {
        CAknQueryDialog::PostLayoutDynInitL();
        CEikButtonGroupContainer& buttons = ButtonGroupContainer();
        buttons.SetCommandL(EEikBidOk, continueLabel);
        buttons.SetCommandL(EEikBidCancel, backLabel);
        buttons.DrawDeferred();
    }

    const TDesC& continueLabel;
    const TDesC& backLabel;
};

static int run_native_query(const TDesC& prompt, const TDesC* continueText, const TDesC* backText)
{
    CEikonEnv *env = CEikonEnv::Static();
    if (!env || !env->EikAppUi()) return -1;
    const TFileName executable_path = RProcess().FileName();
    TParsePtrC executable(executable_path);
    TFileName resource(executable.Drive());
    resource.Append(_L("\\resource\\apps\\cpymo_font_trial.rsc"));
    TInt resource_offset = 0;
    TInt result = 0;
    const char* kind = continueText ? "MO" : "exit";
    native_query_open = ETrue;
    TRAPD(error,
        resource_offset = env->AddResourceFileL(resource);
        CAknQueryDialog *dialog = continueText
            ? new (ELeave) CLegacyMoQuery(*continueText, *backText)
            : CAknQueryDialog::NewL();
        CleanupStack::PushL(dialog);
        dialog->SetPromptL(prompt);
        CleanupStack::Pop(dialog);
        result = dialog->ExecuteLD(continueText ? R_CPYMO_LEGACY_MO_QUERY : R_CPYMO_EXIT_QUERY);
    );
    native_query_open = EFalse;
    if (resource_offset) env->DeleteResourceFile(resource_offset);
    if (error != KErrNone)
        printf("[S60] Native %s query failed: %d.\n", kind, error);
    else
        CPYMO_S60_TRACE("[S60] Native %s query: result=%d.\n", kind, result);
    return error == KErrNone ? (result != 0) : -1;
}

extern "C" int cpymo_s60_native_confirm_exit(int in_game)
{
    const TLanguage language = User::Language();
    TBuf<64> prompt;
    if (language == ELangPrcChinese)
        prompt.Copy(in_game ? _L("\u9000\u51fa CPyMO\uff1f\u8bf7\u5148\u4fdd\u5b58\u8fdb\u5ea6\u3002") : _L("\u9000\u51fa CPyMO\uff1f"));
    else if (language == ELangTaiwanChinese || language == ELangHongKongChinese)
        prompt.Copy(in_game ? _L("\u9000\u51fa CPyMO\uff1f\u8acb\u5148\u5132\u5b58\u9032\u5ea6\u3002") : _L("\u9000\u51fa CPyMO\uff1f"));
    else
        prompt.Copy(in_game ? _L("Exit CPyMO? Save your progress first.") : _L("Exit CPyMO?"));
    return run_native_query(prompt, NULL, NULL);
}

extern "C" int cpymo_s60_native_confirm_legacy_mo(void)
{
    const TLanguage language = User::Language();
    if (language == ELangPrcChinese) {
        const TPtrC proceed = _L("\u7ee7\u7eed");
        const TPtrC back = _L("\u8fd4\u56de");
        return run_native_query(_L("\u672c\u4f5c\u4f7f\u7528\u65e7\u7248\u811a\u672c\n\u8bf7\u7528\u4ee5\u4e0b\u5de5\u5177\u8f6c\u6362\nPyMO Legacy Converter"),
            &proceed, &back);
    }
    if (language == ELangTaiwanChinese || language == ELangHongKongChinese) {
        const TPtrC proceed = _L("\u7e7c\u7e8c");
        const TPtrC back = _L("\u8fd4\u56de");
        return run_native_query(_L("\u672c\u4f5c\u4f7f\u7528\u820a\u7248\u8173\u672c\n\u8acb\u7528\u4ee5\u4e0b\u5de5\u5177\u8f49\u63db\nPyMO Legacy Converter"),
            &proceed, &back);
    }
    const TPtrC proceed = _L("Continue");
    const TPtrC back = _L("Back");
    return run_native_query(_L("This game uses legacy MO scripts\nConvert them with\nPyMO Legacy Converter"),
        &proceed, &back);
}

static void migrate_preferences()
{
    RFs fs;
    if (fs.Connect() != KErrNone) return;
    const TPtrC names[] = { _L("roots.txt"), _L("last.txt"), _L("font-profile.txt") };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        TFileName source(_L("C:\\Data\\CPyMOFontTrial\\"));
        TFileName target(_L("C:\\Data\\CPyMO\\"));
        source.Append(names[i]);
        target.Append(names[i]);
        BaflUtils::CopyFile(fs, source, target, 0);
    }
    fs.Close();
}

// Native file-server tracing deliberately does not use stdio or alter exit behavior.
extern "C" void cpymo_s60_exit_stage(int stage)
{
    if (!diagnostics_enabled) return;
    RFs fs;
    if (fs.Connect() != KErrNone) return;
    const TPtrC path(_L("C:\\Data\\CPyMO\\exit-stages.log"));
    RFile file;
    TInt err = file.Open(fs, path, EFileWrite | EFileShareAny);
    if (err == KErrNotFound || err == KErrPathNotFound) {
        fs.MkDirAll(_L("C:\\Data\\CPyMO\\"));
        err = file.Create(fs, path, EFileWrite | EFileShareAny);
    }
    if (err == KErrNone) {
        TInt end = 0;
        err = file.Seek(ESeekEnd, end);
        if (err == KErrNone) {
            TBuf8<96> line;
            line.Format(_L8("[Exit] stage=%d ticks=%u\n"), stage, User::TickCount());
            file.Write(line);
            file.Flush();
        }
        file.Close();
    }
    fs.Close();
}

class CCPyMOApp : public CSDLApp {
public:
    TUid AppDllUid() const { return TUid::Uid(0xe93c60a3); }
    void GetDataFolder(TDes &folder) { folder.Copy(_L("CPyMO")); }
    void LaunchAppL(int, char **) {
        mkdir("C:/Data/CPyMO",0777);
        migrate_preferences();
        struct stat marker;
        diagnostics_enabled = stat("C:/Data/CPyMO/diagnostics.on", &marker) == 0
            && S_ISREG(marker.st_mode);
        if (diagnostics_enabled) remove("C:/Data/CPyMO/exit-stages.log");
        if (freopen("C:/Data/CPyMO/cpymo.log", "w", stdout)) {
            if (diagnostics_enabled)
                setvbuf(stdout, NULL, _IONBF, 0);
            else
                setvbuf(stdout, log_buffer, _IOFBF, sizeof(log_buffer));
        }
        printf("[App] CPyMO S60v3 1.1.0 startup.\n");
        char program[]="cpymo";char *args[]={program,0};
        int result=SDL_main(1,args);
        cpymo_s60_exit_stage(3);
        printf("[App] SDL_main returned %d.\n", result);
        fflush(stdout);
        // Main has already released game/audio/font/SDL resources and flushed logs.
        // Let the native AppUi/framework close normally; C exit's Open C cleanup
        // did not progress in the measured guest despite stages 1..4 completing.
        cpymo_s60_exit_stage(4);
        CEikonEnv *env = CEikonEnv::Static();
        if (!env || !env->EikAppUi()) {
            cpymo_s60_exit_stage(7);
            User::Leave(KErrNotReady);
        }
        cpymo_s60_exit_stage(5);
        env->EikAppUi()->HandleCommandL(EEikCmdExit);
        cpymo_s60_exit_stage(6); // Exit may return after stopping the active scheduler.
        return;
    }
};
CApaApplication *NewApplication() { return new CCPyMOApp; }
GLDEF_C TInt E32Main() { return EikStart::RunApplication(NewApplication); }
