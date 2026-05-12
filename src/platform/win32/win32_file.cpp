#include "file.hpp"
#include "log.hpp"
#include "string.hpp"
#include "array.hpp"

#include "win32.hpp"

namespace file {

	bool get_size(const char* path, u64* file_size) {
		HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

		if (h == INVALID_HANDLE_VALUE) {
			return false;
		}

		LARGE_INTEGER size;
		if (!GetFileSizeEx(h, &size)) {
			CloseHandle(h);
			return false;
		}

		*file_size = static_cast<u64>(size.QuadPart);
		CloseHandle(h);
		return true;
	}

	u64 read_file(const char* path, void* buffer, u64 buffer_size) {
		HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

		if (h == INVALID_HANDLE_VALUE) {
			logger::error("file read could not open: %s Win32 error %lu", path, GetLastError());
			return 0;
		}

		DWORD to_read = static_cast<DWORD>(buffer_size);
		DWORD bytes_read = 0;

		BOOL ok = ReadFile(h, buffer, to_read, &bytes_read, nullptr);
		CloseHandle(h);

		if (!ok) {
			logger::error("file read could not read: %s Win32 error %lu", path, GetLastError());
			return 0;
		}
		return static_cast<u64>(bytes_read);
	}

	bool write_file(const char* path, const void* data, u64 size) {
		HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE) {
			logger::error("file write could not create: %s Win32 error %lu", path, GetLastError());
			return false;
		}
		DWORD written = 0;
		BOOL ok = WriteFile(h, data, static_cast<DWORD>(size), &written, nullptr);
		CloseHandle(h);
		if (!ok || written != (DWORD)size) {
			logger::error("file write failed: %s Win32 error %lu", path, GetLastError());
			return false;
		}
		return true;
	}

	bool exists(const char* path) {
		DWORD attr = GetFileAttributesA(path);
		return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
	}

	void file_visit(const char* folder, const char* extension, file_visit_fn callback, void* userdata) {
		char pattern[256];
		str::copy(pattern, folder, sizeof(pattern));
		str::concat(pattern, "/*", sizeof(pattern));
		str::concat(pattern, extension, sizeof(pattern));

		WIN32_FIND_DATAA find_data;
		HANDLE h = FindFirstFileA(pattern, &find_data);

		if (h == INVALID_HANDLE_VALUE) return;

		do {
			if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
			if (!callback(find_data.cFileName, userdata)) break;

		} while (FindNextFileA(h, &find_data));

		FindClose(h);
	}

	u32 scan_directory(const char* root, arr::Array<FileEntry>* out) {
		struct DirState {

			HANDLE h;
			WIN32_FIND_DATAA fd;
			char path[256];
			u32 depth;

		};

		const int MAX_DEPTH = 64;
		DirState stack[MAX_DEPTH];
		int top = 0;

		str::copy(stack[top].path, root, sizeof(stack[top].path));
		stack[top].depth = 0;

		char pattern[256];
		str::copy(pattern, root, sizeof(pattern));
		str::concat(pattern, "/*", sizeof(pattern));

		stack[top].h = FindFirstFileA(pattern, &stack[top].fd);

		u32 count = 0;

		while (top >= 0) {
			if (stack[top].h == INVALID_HANDLE_VALUE) {
				top--;
				continue;
			}
			bool dived_into_subdir = false;

			do {
				WIN32_FIND_DATAA& fd = stack[top].fd;
				if (str::equal(fd.cFileName, ".") || str::equal(fd.cFileName, "..")) {
					continue;
				}

				FileEntry entry;
				entry.depth = stack[top].depth;
				str::copy(entry.name, fd.cFileName, sizeof(entry.name));
				str::copy(entry.path, stack[top].path, sizeof(entry.path));
				str::concat(entry.path, "/", sizeof(entry.path));
				str::concat(entry.path, fd.cFileName, sizeof(entry.path));

				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
					entry.is_file = false;
					arr::array_push(out, entry);
					count++;
					if (top + 1 < MAX_DEPTH) {
						bool has_next = FindNextFileA(stack[top].h, &stack[top].fd);
						top++;
						str::copy(stack[top].path, entry.path, sizeof(stack[top].path));
						stack[top].depth = entry.depth + 1;
						char sub_pattern[256];
						str::copy(sub_pattern, stack[top].path, sizeof(sub_pattern));
						str::concat(sub_pattern, "/*", sizeof(sub_pattern));
						stack[top].h = FindFirstFileA(sub_pattern, &stack[top].fd);
						if (!has_next) {
							FindClose(stack[top - 1].h);
							stack[top - 1].h = INVALID_HANDLE_VALUE;
						}

						dived_into_subdir = true;
						break;
					}
				}
				else {
					entry.is_file = true;
					arr::array_push(out, entry);
					count++;
				}

			} while (FindNextFileA(stack[top].h, &stack[top].fd));

			if (!dived_into_subdir) {
				if (stack[top].h != INVALID_HANDLE_VALUE) {
					FindClose(stack[top].h);
				}
				top--;
			}
		}

		return count;
	}

}
