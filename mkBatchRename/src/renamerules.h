#ifndef RENAMERULES_H
#define RENAMERULES_H

#include <QString>

struct RegExRule {
    bool enabled = false;
    QString match;
    QString replace;
    bool includeExtension = false;
};

struct FileNameRule {
    bool enabled = false;
    int mode = 0; // 0: Keep, 1: Remove, 2: Fixed, 3: Reverse
    QString fixedName;
};

struct ReplaceRule {
    bool enabled = false;
    QString match;
    QString replace;
    bool matchCase = false;
};

struct CaseRule {
    bool enabled = false;
    int mode = 0; // 0: Same, 1: Lower, 2: Upper, 3: Title, 4: Sentence
    QString exception;
};

struct RenameRules {
    RegExRule regEx;
    FileNameRule file;
    ReplaceRule replace;
    // ... add sub-structs for Case, Remove, Add, AutoDate, Numbering
};

#endif // RENAMERULES_H
