// XModel (stock model) inspector data: LOD table, collision LOD, bounds, bones and material names. Read-only.
// The input is one structured ACTS export: `acts ... -a xmodel -n <name>` writes
// <out>/mw19replay/<zone>/assets/xmodel/<name>.<n>.asset.json in format "mw19-asset-json" (root_type "XModel").
// Field paths read from that export (fields = asset.fields):
//   format, root_type                            envelope: format starts with "mw19-asset-json", root_type "XModel"
//   fields.name.string                           model name
//   fields.numLods                               LOD count; lodInfo has six slots, only the first numLods are used
//   fields.lodInfo[i].dist                       LOD switch distance, inches
//   fields.lodInfo[i].numsurfs                   surface count of the LOD
//   fields.lodInfo[i].modelSurfsStaging.name     XModelSurfs asset of the LOD (null object when none)
//   fields.collLod                               collision LOD index; the reader keeps it only when it is < numLods
//                                                (the stock exports read 8 with 3 or 6 LODs, so -1 is reported there)
//   fields.radius                                bounding sphere radius, inches
//   fields.bounds.midPoint.v[3], fields.bounds.halfSize.v[3]   bounding box, inches
//   fields.boneNames.values[c].value             scr_string_t id of bone c. The name is entry <id> of the zone's
//                                                script_strings.json (a JSON array, null at index 0); readXModelExport
//                                                looks for that file up to three folders above the export. parseXModelExport
//                                                cannot, so it leaves the name as "#<id>".
//   fields.parentList.bytes                      hex string, one byte per non-root bone; bone c = numRoots + k has parent
//                                                c - byte[k] (byte 0 is invalid). null when every bone is a root.
//                                                numRoots = boneNames count - bytes count.
//   fields.trans.values, fields.quats.values     local translation (3 floats) and rotation (4 int16 x,y,z,w, divided by
//                                                32767) of non-root bone k, relative to its parent. null when all roots.
//   fields.baseMat.values[c].trans.v[3]          model-space pose of bone c (also quat.v[4]). Equal to the parent-chain
//                                                composition of the local transforms in the stock exports; used when present.
//   fields.materialHandles.values[i].name        material reference; a leading ',' is the shared-name marker and is removed
// Positions are inches, Z-up, as exported.
#pragma once

#include <string>
#include <vector>

namespace iw8r {

struct XModelBone {
    std::string name;      // resolved name, or "#<id>" when the script string table was not read
    int parent = -1;       // index into XModelInfo::bones, -1 for a root
    double pos[3] = {0, 0, 0};  // model-space position in inches, Z-up
};

struct XModelLod {
    std::string surfs;        // XModelSurfs asset name of the LOD, empty when none
    double distance = 0;      // switch distance in inches, ascending across the LODs
    int surfCount = 0;        // fields.lodInfo[i].numsurfs
    int surfIndex = 0;        // fields.lodInfo[i].surfIndex: first materialHandles entry of the LOD's surfaces
    std::vector<std::string> materials;  // the LOD's surface materials in surface order (3.18 LOD preview)
};

struct XModelInfo {
    std::string name;                   // fields.name.string
    std::vector<XModelLod> lods;        // the first numLods entries of lodInfo
    int collLod = -1;                   // collision LOD index, or -1 when out of range
    std::vector<XModelBone> bones;      // every entry of boneNames (skeleton bones, then client bones)
    double bounds[6] = {0, 0, 0, 0, 0, 0};  // min x,y,z then max x,y,z, inches
    double radius = 0;                  // bounding sphere radius, inches
    std::vector<std::string> materials; // unique material names, first-seen order
};

// Parses the export text. Returns false and sets *err for input that is not an mw19 XModel export or has an invalid
// bone parent index or LOD table. Bone names stay "#<id>" (see the field path notes above).
bool parseXModelExport(const std::string& jsonText, XModelInfo& out, std::string* err = nullptr);

// Reads `path` and parses it with parseXModelExport, then resolves bone names from the zone's script_strings.json.
// Returns false when the file cannot be read or does not parse.
bool readXModelExport(const std::string& path, XModelInfo& out, std::string* err = nullptr);

// LOD index used at a camera distance (inches): the first LOD whose distance is greater than d (a distance of 0 has no
// limit), as the IW engines' XModelGetLodForDist; -1 past the last LOD (not drawn); 0 for a model without LODs.
int lodForDistance(const XModelInfo& m, double distance);

// LOD preview (3.18): the LOD drawn for a placement at `distance` with the misc_model lod_scale `lodScale` (Replay
// multiplies the switch distances by it, research 54). lodScale <= 0 counts as 1.
int previewLodFor(const XModelInfo& m, double distance, double lodScale);

// True for a tag bone: the name starts with "tag_".
bool isTag(const XModelBone& bone);

}  // namespace iw8r
