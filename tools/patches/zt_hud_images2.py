import io

# Converter (x64-zt worktree), from the review of zt_hud_images.py: the map's HUD images are taken from the
# extraction's images folder only (not from anywhere under the extraction), one file per name (.dds before .iwi), so
# an image of the same name elsewhere cannot become a second asset.

ROOT = 'C:/zsw-iw8r/scriptables/'
p = 'src/convert/iw3_iw8/iw3_fastfile.cpp'
s = io.open(ROOT + p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
start = s.index('std::size_t CopyHudImages(const std::filesystem::path &extracted, const std::filesystem::path &prepared)')
end = s.index('unsigned FootstepMaterial(std::string material, const RenderPlan &renderPlan)', start)
new = '''std::size_t CopyHudImages(const std::filesystem::path &extracted, const std::filesystem::path &prepared)
{
    std::size_t copied = 0;
    std::set<std::string> taken;
    // .dds first: a name that exists in both forms is carried once.
    for (const char *wanted : {".dds", ".iwi"})
    {
        std::error_code error;
        for (std::filesystem::directory_iterator iterator(extracted / "images", error), end;
             !error && iterator != end; iterator.increment(error))
        {
            std::string extension = iterator->path().extension().string();
            std::string stem = iterator->path().stem().string();
            const auto lower = [](const unsigned char value) { return static_cast<char>(std::tolower(value)); };
            std::ranges::transform(extension, extension.begin(), lower);
            std::ranges::transform(stem, stem.begin(), lower);
            if (!iterator->is_regular_file(error) || extension != wanted || !stem.starts_with("hud_") ||
                !taken.insert(stem).second)
                continue;
            const auto destination = prepared / "images" / (stem + extension);
            std::filesystem::create_directories(destination.parent_path());
            std::filesystem::copy_file(iterator->path(), destination, std::filesystem::copy_options::overwrite_existing);
            zt::info("iw3: included HUD image %s", destination.filename().string().c_str());
            ++copied;
        }
    }
    return copied;
}

'''.replace('\n', nl)
s = s[:start] + new + s[end:]
io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
