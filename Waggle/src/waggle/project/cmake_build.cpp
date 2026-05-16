#include <waggle/project/cmake_build.h>

#include <hive/core/log.h>

#include <wax/containers/string.h>

#include <comb/default_allocator.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace waggle
{
    namespace
    {
        const hive::LogCategory LOG_CMAKE{"Waggle.CMakeBuild"};

        void AppendQuoted(wax::String& dest, wax::StringView value)
        {
            dest.Append("\"", 1);
            dest.Append(value.Data(), value.Size());
            dest.Append("\"", 1);
        }

        wax::String BuildConfigureCommand(const CMakeBuildRequest& request, comb::DefaultAllocator& alloc)
        {
            wax::String cmd{alloc};
            cmd.Append("cmake", 5);
            if (!request.m_presetName.IsEmpty())
            {
                cmd.Append(" --preset ", 10);
                AppendQuoted(cmd, request.m_presetName);
                return cmd;
            }
            if (!request.m_generator.IsEmpty())
            {
                cmd.Append(" -G ", 4);
                AppendQuoted(cmd, request.m_generator);
            }
            if (!request.m_sourceDir.IsEmpty())
            {
                cmd.Append(" -S ", 4);
                AppendQuoted(cmd, request.m_sourceDir);
            }
            if (!request.m_binaryDir.IsEmpty())
            {
                cmd.Append(" -B ", 4);
                AppendQuoted(cmd, request.m_binaryDir);
            }
            if (!request.m_config.IsEmpty())
            {
                cmd.Append(" -DCMAKE_BUILD_TYPE=", 20);
                cmd.Append(request.m_config.Data(), request.m_config.Size());
            }
            return cmd;
        }

        wax::String BuildBuildCommand(const CMakeBuildRequest& request, comb::DefaultAllocator& alloc)
        {
            wax::String cmd{alloc};
            cmd.Append("cmake --build ", 14);
            AppendQuoted(cmd, request.m_binaryDir);
            if (!request.m_config.IsEmpty())
            {
                cmd.Append(" --config ", 10);
                cmd.Append(request.m_config.Data(), request.m_config.Size());
            }
            if (!request.m_target.IsEmpty())
            {
                cmd.Append(" --target ", 10);
                cmd.Append(request.m_target.Data(), request.m_target.Size());
            }
            return cmd;
        }

#if defined(_WIN32)
        int RunCommand(const wax::String& commandLine, CMakeLogCallback callback, void* userdata)
        {
            HANDLE readEnd = nullptr;
            HANDLE writeEnd = nullptr;
            SECURITY_ATTRIBUTES sa{};
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = TRUE;
            sa.lpSecurityDescriptor = nullptr;
            if (CreatePipe(&readEnd, &writeEnd, &sa, 0) == 0)
            {
                hive::LogError(LOG_CMAKE, "CreatePipe failed");
                return -1;
            }
            SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOA si{};
            si.cb = sizeof(si);
            si.hStdError = writeEnd;
            si.hStdOutput = writeEnd;
            si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            si.dwFlags = STARTF_USESTDHANDLES;

            PROCESS_INFORMATION pi{};
            wax::String mutableCmd{commandLine};
            mutableCmd.Append("\0", 1);

            const BOOL ok = CreateProcessA(nullptr, mutableCmd.Data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                           nullptr, nullptr, &si, &pi);
            CloseHandle(writeEnd);
            if (ok == 0)
            {
                CloseHandle(readEnd);
                hive::LogError(LOG_CMAKE, "CreateProcessA failed for: {}", commandLine.CStr());
                return -1;
            }

            char buffer[1024];
            wax::String lineAccum{comb::GetDefaultAllocator()};
            for (;;)
            {
                DWORD bytesRead = 0;
                const BOOL readOk = ReadFile(readEnd, buffer, sizeof(buffer) - 1, &bytesRead, nullptr);
                if (readOk == 0 || bytesRead == 0)
                {
                    break;
                }
                for (DWORD i = 0; i < bytesRead; ++i)
                {
                    const char c = buffer[i];
                    if (c == '\n' || c == '\r')
                    {
                        if (lineAccum.Size() > 0)
                        {
                            if (callback != nullptr)
                            {
                                callback(lineAccum.CStr(), false, userdata);
                            }
                            lineAccum.Clear();
                        }
                    }
                    else
                    {
                        lineAccum.Append(&c, 1);
                    }
                }
            }
            if (lineAccum.Size() > 0 && callback != nullptr)
            {
                callback(lineAccum.CStr(), false, userdata);
            }

            CloseHandle(readEnd);
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD exitCode = 0;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return static_cast<int>(exitCode);
        }
#else
        int RunCommand(const wax::String& commandLine, CMakeLogCallback callback, void* userdata)
        {
            int pipefd[2]{};
            if (pipe(pipefd) != 0)
            {
                hive::LogError(LOG_CMAKE, "pipe() failed");
                return -1;
            }

            const pid_t pid = fork();
            if (pid < 0)
            {
                close(pipefd[0]);
                close(pipefd[1]);
                hive::LogError(LOG_CMAKE, "fork() failed");
                return -1;
            }
            if (pid == 0)
            {
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                dup2(pipefd[1], STDERR_FILENO);
                close(pipefd[1]);
                execl("/bin/sh", "sh", "-c", commandLine.CStr(), nullptr);
                _exit(127);
            }
            close(pipefd[1]);

            char buffer[1024];
            wax::String lineAccum{comb::GetDefaultAllocator()};
            for (;;)
            {
                const ssize_t bytesRead = read(pipefd[0], buffer, sizeof(buffer) - 1);
                if (bytesRead <= 0)
                {
                    break;
                }
                for (ssize_t i = 0; i < bytesRead; ++i)
                {
                    const char c = buffer[i];
                    if (c == '\n' || c == '\r')
                    {
                        if (lineAccum.Size() > 0)
                        {
                            if (callback != nullptr)
                            {
                                callback(lineAccum.CStr(), false, userdata);
                            }
                            lineAccum.Clear();
                        }
                    }
                    else
                    {
                        lineAccum.Append(&c, 1);
                    }
                }
            }
            if (lineAccum.Size() > 0 && callback != nullptr)
            {
                callback(lineAccum.CStr(), false, userdata);
            }

            close(pipefd[0]);
            int status = 0;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status))
            {
                return WEXITSTATUS(status);
            }
            return -1;
        }
#endif
    } // namespace

    int RunCMakeConfigure(const CMakeBuildRequest& request, CMakeLogCallback callback, void* userdata)
    {
        const wax::String cmd = BuildConfigureCommand(request, comb::GetDefaultAllocator());
        hive::LogInfo(LOG_CMAKE, "Configure: {}", cmd.CStr());
        if (callback != nullptr)
        {
            wax::String announce{comb::GetDefaultAllocator()};
            announce.Append("$ ", 2);
            announce.Append(cmd.Data(), cmd.Size());
            callback(announce.CStr(), false, userdata);
        }
        return RunCommand(cmd, callback, userdata);
    }

    int RunCMakeBuild(const CMakeBuildRequest& request, CMakeLogCallback callback, void* userdata)
    {
        const wax::String cmd = BuildBuildCommand(request, comb::GetDefaultAllocator());
        hive::LogInfo(LOG_CMAKE, "Build: {}", cmd.CStr());
        if (callback != nullptr)
        {
            wax::String announce{comb::GetDefaultAllocator()};
            announce.Append("$ ", 2);
            announce.Append(cmd.Data(), cmd.Size());
            callback(announce.CStr(), false, userdata);
        }
        return RunCommand(cmd, callback, userdata);
    }
} // namespace waggle
