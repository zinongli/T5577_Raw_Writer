# T5577 Raw Writer App
An easy to use T5577 raw writer app for Flipper Zero. [Discord project page.](https://discord.com/channels/1211622338198765599/1267190551783018659)
## Instruction:

Configure the modulation, RF Clock, and number of blocks in the `Config` menu. The edit block feature is useless right not but will be implemented to allow editing user blocks in the future. 

Or, you can load a .t5577 file into the app and write it. An example file can be found [here](https://github.com/zinongli/T5577_Raw_Writer/blob/b8f459c787802a443b2a943250b2ad8249f7c0ea/examples/Tag_1.t5577). The configuration will be automatically loaded from block 0 data. 

The texts like:
`Modulation: ASK/MC
RF Clock: 64
Number of User Blocks: 8`
in the .t5577 files are derived from the block 0 data when saved. So if you want to adjust the configuration, you can simply edit block 0 data, without having to edit the text in the file. Or, you can load the data directly and adjust the configuration in the app before writing tags.

You can also save the data you've just loaded and/or configured. 

## Updates
As of 2024-7-28, this app only supports writing from the file and load it into the app to write. 
The feature of add manually is being developed and should be ready soon. 


