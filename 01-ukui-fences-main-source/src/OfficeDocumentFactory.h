#pragma once

#include <QString>

namespace OfficeDocumentFactory {

// Creates either a real, empty OOXML package (.docx/.xlsx/.pptx) or an empty
// regular file for the remaining suffixes exposed by the New menu.
bool createBlankFile(const QString &filePath, QString *errorMessage = nullptr);

} // namespace OfficeDocumentFactory
