#pragma once

class QWidget;

/* The virtual camera only exists once its DirectShow filter is registered
 * with Windows, which the official installer does and a zip copy does not.
 * This runs the bundled installer with administrator rights, waits for it
 * and reports whether it succeeded. OBS has to restart to pick it up. */
bool SwitcherInstallVirtualCam(QWidget *parent);
