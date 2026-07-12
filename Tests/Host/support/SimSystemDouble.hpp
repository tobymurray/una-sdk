#pragma once

// Test double control hooks for the simulator Mock::System / SystemGUI layer.
//
// The real SensorManager thread (SensorManager.cpp) only runs its refresh pass
// while SDK::Simulator::Mock::SystemGUI::isAppRunning() is true, and the Manager
// singleton joins that thread in its destructor. In a host-test binary there is
// no GUI/HAL to flip that flag, so these hooks let a test gate the manager
// thread's lifetime (in particular, stop it cleanly at global teardown so the
// singleton destructor's join() does not hang at process exit).

namespace simtest {

// Sets the value SystemGUI::isAppRunning() reports. Pass false to let the
// manager thread return from its loop.
void setManagerRunning(bool running);

bool managerRunning();

} // namespace simtest
