#pragma once

// An exclusive lock on a file, seen by every process and by every module in one process: two
// plugins in one host (G1-Emu and G1-Emu Micro are two libraries) each open the file, and the
// locks of two opens conflict (flock, LockFileEx), where fcntl's (JUCE's InterProcessLock) do not
// within one process. The system lets it go if the process dies.

#include <filesystem>
#include <string>
#include <system_error>

#ifdef _WIN32
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <windows.h>
#else
#	include <fcntl.h>
#	include <sys/file.h>
#	include <unistd.h>
#endif

namespace g1app
{
	class FileLock
	{
	public:
		// _wait: block until it is free; otherwise locked() says whether it was taken.
		explicit FileLock(const std::string& _path, const bool _wait = true)
		{
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(_path).parent_path(), ec);
#ifdef _WIN32
			m_handle = CreateFileW(std::filesystem::path(_path).wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if(m_handle != INVALID_HANDLE_VALUE)
			{
				OVERLAPPED o{};
				m_locked = LockFileEx(m_handle, LOCKFILE_EXCLUSIVE_LOCK | (_wait ? 0 : LOCKFILE_FAIL_IMMEDIATELY), 0, 1, 0, &o) != 0;
			}
#else
			m_fd = ::open(_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
			if(m_fd >= 0)
				m_locked = ::flock(m_fd, LOCK_EX | (_wait ? 0 : LOCK_NB)) == 0;
#endif
		}
		~FileLock()
		{
#ifdef _WIN32
			if(m_handle != INVALID_HANDLE_VALUE)
			{
				if(m_locked)
				{
					OVERLAPPED o{};
					UnlockFileEx(m_handle, 0, 1, 0, &o);
				}
				CloseHandle(m_handle);
			}
#else
			if(m_fd >= 0)
			{
				if(m_locked)
					::flock(m_fd, LOCK_UN);
				::close(m_fd);
			}
#endif
		}
		bool locked() const { return m_locked; }
		FileLock(const FileLock&) = delete;
		FileLock& operator=(const FileLock&) = delete;
	private:
#ifdef _WIN32
		HANDLE m_handle = INVALID_HANDLE_VALUE;
#else
		int m_fd = -1;
#endif
		bool m_locked = false;
	};
}
