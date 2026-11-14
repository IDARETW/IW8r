import io

# Converter (x64-zt worktree): a map's own HUD images (IW8 Radiant ROADMAP 4.27, research 36 section 8.2.1).
# An image named hud_* in the extraction's images is carried into the map zone with a 2D material of the same name
# (the one the minimap uses), so a map script can draw it with setshader("hud_<name>", w, h). Until now only the
# minimap image had a material; any other image no world material sampled was left out.

ROOT = 'C:/zsw-iw8r/scriptables/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, n=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    write(p, s.replace(a, b))


F = 'src/convert/iw3_iw8/iw3_fastfile.cpp'
patch(F, '''unsigned FootstepMaterial(std::string material, const RenderPlan &renderPlan)
{''', '''// The map's own HUD images (images named hud_*): copied beside the minimap so the map step converts them and gives
// each a 2D material of its name (convert.cpp addMapAssets). Returns how many were copied.
std::size_t CopyHudImages(const std::filesystem::path &extracted, const std::filesystem::path &prepared)
{
    std::size_t copied = 0;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(extracted, error), end;
         !error && iterator != end; iterator.increment(error))
    {
        std::string extension = iterator->path().extension().string();
        std::string stem = iterator->path().stem().string();
        const auto lower = [](const unsigned char value) { return static_cast<char>(std::tolower(value)); };
        std::ranges::transform(extension, extension.begin(), lower);
        std::ranges::transform(stem, stem.begin(), lower);
        if (!iterator->is_regular_file(error) || (extension != ".dds" && extension != ".iwi") || !stem.starts_with("hud_"))
            continue;
        const auto destination = prepared / "images" / (stem + extension);
        std::filesystem::create_directories(destination.parent_path());
        std::filesystem::copy_file(iterator->path(), destination, std::filesystem::copy_options::overwrite_existing);
        zt::info("iw3: included HUD image %s", destination.filename().string().c_str());
        ++copied;
    }
    return copied;
}

unsigned FootstepMaterial(std::string material, const RenderPlan &renderPlan)
{''')
patch(F, '''    CopyCompass(extracted, result.root, sourceMap, options.map);''', '''    CopyCompass(extracted, result.root, sourceMap, options.map);
    CopyHudImages(extracted, result.root);''')
patch(F, '''            if (lower.starts_with("compass_map_"))
                return {"converted", "minimap: converted by the compass step"};''', '''            if (lower.starts_with("compass_map_"))
                return {"converted", "minimap: converted by the compass step"};
            if (lower.starts_with("hud_"))
                return {"converted", "HUD image of the map: carried with a 2D material of its name"};''')

C = 'src/convert/iw3_iw8/convert.cpp'
patch(C, '''    const std::string compassName = "compass_map_" + map;
    bool hasCompass = false;
    for (const std::string &name : dumpimg::listImageDumps(dumpDirectory))
    {
        const dumpimg::ImageDumpFile input = dumpimg::readImageDump(dumpDirectory, name);
        const unsigned rotation = input.name == compassName ? compassRotation : 0;
        if (!input.loaded || !dumpimg::addImageAsset(writer, dumpimg::convertImage(input, rotation)))
        {
            ++tally.failures;
            if (tally.firstFailure.empty())
                tally.firstFailure = "image " + name + " in " + dumpDirectory;
            continue;
        }
        ++tally.images;
        hasCompass = hasCompass || input.name == compassName;
    }''', '''    const std::string compassName = "compass_map_" + map;
    bool hasCompass = false;
    std::vector<std::string> hudImages; // the map's own HUD images (hud_*): each gets a 2D material of its name
    for (const std::string &name : dumpimg::listImageDumps(dumpDirectory))
    {
        const dumpimg::ImageDumpFile input = dumpimg::readImageDump(dumpDirectory, name);
        const unsigned rotation = input.name == compassName ? compassRotation : 0;
        if (!input.loaded || !dumpimg::addImageAsset(writer, dumpimg::convertImage(input, rotation)))
        {
            ++tally.failures;
            if (tally.firstFailure.empty())
                tally.firstFailure = "image " + name + " in " + dumpDirectory;
            continue;
        }
        ++tally.images;
        hasCompass = hasCompass || input.name == compassName;
        if (input.name.starts_with("hud_"))
            hudImages.push_back(input.name);
    }
    for (const std::string &name : hudImages)
    {
        if (!convdump::mtl::writeUiMaterial(techsets, name, name))
        {
            ++tally.failures;
            if (tally.firstFailure.empty())
                tally.firstFailure = "HUD material " + name;
            continue;
        }
        replayrender::RegisterMaterialReference(writer, name);
        ++tally.materials;
        info("assets: HUD image %s with its 2D material (setshader(\\"%s\\", w, h) in the map script)", name.c_str(), name.c_str());
    }''')
patch(C, '''                else if (name == "compass_map_" + map)
                    origin = "IW3 minimap image";
                break;
            case ASSET_TYPE_MATERIAL:
                if (name == "compass_map_" + map)
                    origin = "minimap material for the IW3 minimap image";
                break;''', '''                else if (name == "compass_map_" + map)
                    origin = "IW3 minimap image";
                else if (name.starts_with("hud_"))
                    origin = "HUD image of the map";
                break;
            case ASSET_TYPE_MATERIAL:
                if (name == "compass_map_" + map)
                    origin = "minimap material for the IW3 minimap image";
                else if (name.starts_with("hud_"))
                    origin = "2D material of the map's HUD image";
                break;''')
print('ok')
