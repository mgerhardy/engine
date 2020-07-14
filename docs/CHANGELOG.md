# Changelog

A more detailed changelog can be found on [github](https://github.com/mgerhardy/engine/commits/).

## 0.0.5 (2020-XX-XX)

Client:

   - Fixed movement

Server:

   - Fixed visibility check

VoxEdit:

   - Started to add scene mode edit support (move volumes)

VoxConvert:

   - Support different palette files (cvar `palette`)
   - Support writing outside the registered application paths
   - Allow to overwrite existing files

General:

   - Switched to qb as default volume format
   - Improved scene graph support for Magicavoxel vox files
   - Fixed invisible voxels for qb and qbt (Qubicle) volume format
   - Support automatic loading different volume formats for assets
   - Support Command&Conquer vxl files
   - Support Ace of Spades map files (vxl)
   - Perform mesh extraction in dedicated threads for simple volume rendering
   - Improved gizmo rendering and translation support
   - Fixed memory leaks on shutdown
   - Improved profiling support via tracy
   - Fixed segfault while removing npcs

## 0.0.4 (2020-06-07)

General:

   - Added support for writing binvox files
   - Added support for reading kvx (Build-Engine) and kv6 (SLAB6) voxel volumes
   - Performed some AFL hardening on voxel format code
   - Don't execute keybindings if the console is active
   - Added basic shader storage buffer support
   - Reduced voxel vertex size from 16 to 8 bytes
   - Apply checkerboard pattern to water surface
   - Improved tracy profiling support
   - A few highdpi fixes

Server:

   - Allow to specify the database port
   - Fixed loading database chunks

VoxEdit:

   - Added `scale` console command to produce LODs

VoxConvert:

   - Added ability to merge all layers into one

## 0.0.3 (2020-05-17)

Assets:

   - Added music tracks
   - Updated and added some new voxel models

VoxEdit:

   - Made some commands available to the ui
   - Tweak `thicken` command
   - Updated default tree generation ui values
   - Save layers to all supported formats
   - Fixed tree generation issue for some tree types
   - Changed default reference position to be at the center bottom
   - Reduced max supported volume size

General:

   - Print stacktraces on asserts
   - Improved tree generation (mainly used in voxedit)
   - Fixed a few asserts in debug mode for the microsoft stl
   - Added debian package support
   - Fixed a few undefined behaviour issues and integer overflows that could lead to problems
   - Reorganized some modules to speed up compilation and linking times
   - Improved audio support
   - Fixed timing issues
   - Fixed invalid GL states after deleting objects

VoxConvert:

   - Added a new tool to convert different voxel volumes between supported formats
     Currently supported are cub (CubeWorld), vox (MagicaVoxel), vmx (VoxEdit Sandbox), binvox
     and qb/qbt (Qubicle)

Client:

   - Added footstep and ambience sounds

## 0.0.2 (2020-05-06)

VoxEdit:

   - Static linked VC++ Runtime
   - Extract voxels by color into own layers
   - Updated tree and noise windows
   - Implemented `thicken` console command
   - Escape abort modifier action
   - Added L-System panel

General:

   - Fixed binvox header parsing
   - Improved compilation speed
   - Fixed compile errors with locally installed glm 0.9.9
   - Fixed setup-documentation errors
   - Fixed shader pipeline rebuilds if included shader files were modified
   - Improved palm tree generator
   - Optimized mesh extraction for the world (streaming volumes)
   - Added new voxel models
   - (Re-)added Tracy profiler support and removed own imgui-based implementation
   - Fixed writing of key bindings
   - Improved compile speed and further removed the STL from a lot of places
   - Updated all dependencies to their latest version

Server/Client:

   - Added DBChunkPersister
   - Built-in HTTP server to download the chunks
   - Replaced ui for the client

Voxel rendering

   - Implemented reflection for water surfaces
   - Apply checkerboard pattern to voxel surfaces
   - Up-scaling effect for new voxel chunks while they pop in
   - Optimized rendering by not using one giant vbo


## 0.0.1 "Initial Release" (2020-02-08)

VoxEdit:

   - initial release
