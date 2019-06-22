# stoermelder µMAP

µMAP is a single slot of [CV-MAP](./CVMap.md) with attenuvation and output port. Please refer to the manual of CV-MAP how to this module works.

### Section OFFSET

Using the trimpod the input signal can be offseted in the range 0% to 100%. Connecting a cable to the offset-port disables the trimpod. It expects voltage 0-10V.

### Section SCALE

The input signal can be scaled using the trimpod from -200% to 200% while its default value is 100%. Connecting a cable to the scale-port disables the trimpod. It expects voltage -10V to +10V which is mapped to scaling -200% to 200%.