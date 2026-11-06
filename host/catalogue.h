// 关卡目录（levels/catalogue.json）：主界面与 host 的唯一数据来源。
// 每个关卡都会在启动时解析并校验，
// 因此非法的目录会快速失败（任务书 4.8，E1019）。
#pragma once

#include <string>
#include <vector>

#include "fc/level.h"

namespace fc {

struct CatalogueEntry {
    std::string id;
    std::string name;
    std::string subtitle;
    std::string tag;
    std::string accent;
    std::string file;
    int order = 0;
    int difficulty = 1;
    LevelDefinition level;  // 加载时解析并校验
};

struct Catalogue {
    int version = 1;
    std::vector<CatalogueEntry> levels;
    std::string command;  // 证据：目录是如何生成的

    int indexOf(const std::string& levelId) const;
};

struct CatalogueResult {
    bool ok = false;
    Catalogue catalogue;
    LevelError error;
};

// 加载目录文档，然后加载它引用的每个关卡文件。
// `root` 是项目根目录，用于解析相对的 `file` 字段。
CatalogueResult loadCatalogue(const std::string& jsonText, const std::string& root);

// 单个关卡的预览，由关卡数据生成（任务书 7.4.1）：
// {rows, cols, road:[[r,c]...], fruits:[[r,c,"red"]...], island:[[r,c]...]}
std::string previewJson(const LevelDefinition& level);

// GET /levels 的载荷：{version, current, levels:[...]}，其中每个关卡附带预览。
std::string catalogueJson(const Catalogue& catalogue, int currentIndex);

}  // namespace fc
