#include "C:\git\cppwinrt\DevProjection\winrt\base.h"
#include <filesystem>
#include <set>
#include <algorithm>
#include <string.h>

using namespace winrt;
using namespace Windows::Foundation;
using namespace std::chrono;
using namespace std::experimental::filesystem;
using namespace std::string_view_literals;

struct handle_traits
{
    using type = HANDLE;

    static void close(HANDLE value) noexcept
    {
        WINRT_VERIFY_(TRUE, CloseHandle(value));
    }

    static constexpr HANDLE invalid() noexcept
    {
        return nullptr;
    }
};

using handle = impl::handle<handle_traits>;

struct file_handle_traits
{
    using type = HANDLE;

    static void close(HANDLE value) noexcept
    {
        WINRT_VERIFY_(TRUE, CloseHandle(value));
    }

    static constexpr HANDLE invalid() noexcept
    {
        return INVALID_HANDLE_VALUE;
    }
};

using file_handle = impl::handle<file_handle_traits>;

struct file_view
{
    explicit file_view(std::wstring const& name) noexcept
    {
        file_handle const file = CreateFile(name.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (!file) { return; }

        WINRT_VERIFY(GetFileSizeEx(file.get(), reinterpret_cast<LARGE_INTEGER*>(&m_size)));
        if (m_size == 0) { return; }

        handle const map = CreateFileMapping(file.get(), nullptr, PAGE_READONLY, 0, 0, nullptr);
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
static std::map<std::wstring, std::string> paths;
static std::map<std::string, std::set<std::wstring>> versions;
static std::mutex store_lock;

void store(std::wstring const& filename, std::string const& version)
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
}

void print()
{
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
}

IAsyncAction find_version(std::wstring const filename)
{
    co_await resume_background();
    std::string const version = get_version(filename);

    if (!version.empty())
    {
        store(filename, version);
    }
}

template <typename Collection>
void get_all(Collection const& collection)
{
    for (auto&& item : collection)
    {
        item.get();
    }
}

int wmain(int argc, wchar_t** argv)
{
    auto start = high_resolution_clock::now();

    if (argc == 2 && 0 == wcscmp(argv[1], L"-v"))
    {
        dash_version = true;
    }

    std::vector<IAsyncAction> finders;

    for (auto&& item : recursive_directory_iterator(current_path()))
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

    get_all(finders);
    print();
    printf("\nTime: %llums\n", duration_cast<milliseconds>(high_resolution_clock::now() - start).count());
}