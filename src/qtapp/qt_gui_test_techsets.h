#pragma once
#include <QString>
#include <functional>
namespace iw8r::qt {
class MainWindow;
void runTechsetChecks(std::function<void(const QString &)> section, std::function<void(bool, const QString &)> check,
                      MainWindow &window, const QString &out);
} // namespace iw8r::qt
