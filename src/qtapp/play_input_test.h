// IW8 Radiant Suite - play preview input check through the camera view's real key path (research 140).
#pragma once

#include <QString>

namespace iw8r::qt {

class MainWindow;

// IW8R_TEST_PLAY_INPUT=<script file>: drives the play preview with QTest key presses sent to the camera view (the
// shortcut override, keyPressEvent / keyReleaseEvent, the held keys and PlayControls, as a real keyboard does) and
// prints the player state over time to stderr. Script lines (# comments):
//   start <x> <y> <z> <yaw>   stop any play, put the camera there (the player lands on the floor below) and play
//   down <key> | up <key>     key names: W A S D SPACE C CTRL SHIFT F H N ESC
//   tap <key>                 down, 100 ms, up
//   wait <ms>                 the state is printed every `every` ms meanwhile
//   every <ms>                print interval (0: only at log lines)
//   look <yaw> <pitch>        set the view angles
//   log <text>                print the state with a label
//   quit                      exit with code 0
void startPlayInputTest(MainWindow& w, const QString& scriptPath);

}  // namespace iw8r::qt
