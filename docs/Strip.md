# stoermelder STRIP

STRIP is a module for managing a strip of modules. A "strip" is a group of modules formed in a single row of Rack without any empty spaces between them, directly next to STRIP on the left or right side. STRIP uses Rack v1's expander-mechanism to traverse the modules in a group. Changing the modules in a strip is simply moving them a bit in Rack and creating an empty space between the rest of the modules. 

![STRIP Intro](./Strip-intro.gif)

### MODE

STRIP can be configured which modules will be considered as managed. By default STRIP handles all modules left and right of itself, but the MODE-button allows you to toggle to left-only or right-only. Which modules are affected is shown by the LEDs next to the direction-triangles on the panel.

### ON, OFF, RAND

These ports and buttons enable CV control of options only available in the context menu of modules: ON and OFF triggers "Disable" in different modes, RAND triggers "Randomize". 

![STRIP RAND](./Strip-rand.gif)

The modes for the ON-section are:

- Default: ON enables the modules, OFF disables the modules.
- Toggle: ON toggles enable/disable of the modules, OFF disables the modules.
- High/Low: ON in high-state (button pressed or voltage >=1V) enables the modules, ON in low-state (voltage <1V) disables the modules.

### Loading and saving, copying and pasting strips

A useful feature of STRIP is its ability to save strips to files and load them afterwards. This allows you to build blocks of modules for some purpose (e.g. synth voices, fx sections, mixer consoles) and reuse them later in a different patch or quickly clone them within the same patch.

![STRIP context menu](./Strip-context.jpg)

All settings of the modules, all cables (and color) between modules within the strip and all parameter mappings (e.g. by VCV MIDI-MAP or stoermelder CV-MAP) are saved. Also, copy and paste is available, by option in the context menu or by using keyboard shortcuts Shift-C and Shift+V. When using left-only or right-only mode only affected modules will be saved and loaded, even if a different mode was used for saving or copying.

![STRIP copy and paste](./Strip-copy.gif)

The file-format "vcvss" for storing strips is very close to Rack's own format for storing patches or module presets. Compatibility in future versions should be no problem as long Rack will be able to load preset files from older Rack versions.

This module was added in v1.0.4 of PackOne.