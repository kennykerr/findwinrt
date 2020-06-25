#include <Windows.h>
#include <winrt/Windows.Foundation.h>
#include <filesystem>
#include <set>
#include <algorithm>
#include <mutex>
#include <string.h>

using namespace winrt;
using namespace Windows::Foundation;
using namespace std::chrono;
using namespace std::filesystem;
using namespace std::string_view_literals;

struct file_view
{
    explicit file_view(std::wstring const& name) noexcept
    {
        file_handle const file(CreateFile(name.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file) { return; }

        WINRT_VERIFY(GetFileSizeEx(file.get(), reinterpret_cast<LARGE_INTEGER*>(&m_size)));
        if (m_size == 0) { return; }

        handle const map(CreateFileMapping(file.get(), nullptr, PAGE_READONLY, 0, 0, nullptr));
        if (!map) { return; }

        m_view = static_cast<char const *>(MapViewOfFile(map.get(), FILE_MAP_READ, 0, 0, 0));
    }

    ~file_view() noexcept
    {
        if (m_view)
        {
            WINRT_VERIFY(UnmapViewOfFile(m_view));
        }
    }

    explicit operator bool() const noexcept
    {
        return m_view != nullptr;
    }

    char const* begin() const noexcept
    {
        return m_view;
    }

    char const* end() const noexcept
    {
        return m_view + m_size;
    }

private:
    char const* m_view{};
    uint64_t m_size{};
};

std::string get_version(std::wstring const& filename)
{
    file_view const file{ filename };
    constexpr auto const find{ "C++/WinRT version:"sv };
    char const* begin = file.begin();

    while (begin = static_cast<char const*>(memchr(begin, find.front(), file.end() - begin)))
    {
        if (static_cast<size_t>(file.end() - begin) > find.size())
        {
            if (0 == find.compare(0, find.size(), begin, find.size()))
            {
                begin += find.size();
                break;
            }
        }

        ++begin;
    }

    if (begin)
    {
        char const* end = begin;

        for (; *end == '.' || isdigit(*end); ++end);

        if (begin != end)
        {
            return { begin, end };
        }
    }

    return {};
}

static bool dash_version = false;
static bool dash_time = false;
static bool dash_unique = false;
static std::map<std::wstring, std::string> paths;
static std::map<std::string, std::set<std::wstring>> versions;
static std::set<std::wstring> unique;
static std::mutex store_lock;

IAsyncAction find_version(path const filename)
{
    co_await resume_background();
    std::string const version = get_version(filename);

    if (!version.empty())
    {
        std::lock_guard<std::mutex> guard(store_lock);

        if (dash_version)
        {
            versions[version].insert(filename);
        }
        else
        {
            paths[filename] = version;
        }

        if (dash_unique)
        {
            unique.insert(filename.filename());
        }
    }
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        auto start = high_resolution_clock::now();

        for (int arg = 1; arg < argc; ++arg)
        {
            if (0 == wcscmp(argv[arg], L"-v"))
            {
                dash_version = true;
            }
            else if (0 == wcscmp(argv[arg], L"-t"))
            {
                dash_time = true;
            }
            else if (0 == wcscmp(argv[arg], L"-u"))
            {
                dash_unique = true;
            }
            else
            {
                printf(R"(
    Searches for binaries built with C++/WinRT
    Created by Kenny Kerr

    findwinrt.exe [options...]

      -v Sort output by version
      -u Show unique file names
      -t Show search time
    )");

                return 0;
            }
        }

        std::vector<IAsyncAction> finders;

        for (auto&& item : recursive_directory_iterator(current_path(), directory_options::skip_permission_denied))
        {
            if (is_directory(item))
            {
                continue;
            }

            std::wstring extension = item.path().extension();
            std::transform(extension.begin(), extension.end(), extension.begin(), towlower);

            if (extension != L".exe" && extension != L".dll")
            {
                continue;
            }

            finders.push_back(find_version(item.path()));
        }

        for (auto&& item : finders)
        {
            item.get();
        }

        if (dash_version)
        {
            for (auto&& version : versions)
            {
                printf("\n[%s]\n", version.first.c_str());

                for (auto&& filename : version.second)
                {
                    printf("%ls\n", filename.c_str());
                }
            }
        }
        else
        {
            for (auto&& path : paths)
            {
                printf("[%s] %ls\n", path.second.c_str(), path.first.c_str());
            }
        }

        if (dash_unique)
        {
            printf("\n[unique]\n");

            for (auto&& filename : unique)
            {
                printf("%ls\n", filename.c_str());
            }
        }

        if (dash_time)
        {
            printf("\nTime: %llus\n", duration_cast<seconds>(high_resolution_clock::now() - start).count());
        }
    }
    catch (std::exception const& e)
    {
        printf("Error: %s\n", e.what());
    }
}