# CCP4/Coot/Privateer Web Assembly
This project is a set of scripts, patches and CMakeLists.txt files which enable the compilation of some of the CCP4 libraries, some of Coot, FFTW2, Privateer and the Gnu Scientific Library to Web Assembly.

### Todo

- [ ] Get a list of Rotamer Chi Angles for a given residue (type)
- [ ] Use map and model find peptides that need to be flipped
- [ ] Centre-on in Display Table
- [ ] Separate react components from library JS.
- [ ] Better clip/fog defaults
- [ ] mini-rsr example
  - [x] Compile
  - [x] Run in node (sort of ... does not exit at end).
  - [ ] Run in browser
    - [x] Runs (mostly successfully).
    - [x] Access to metal distances tables. (Pre-load?)
    - [x] Access to partial monomer library in FS.
    - [x] Access to FS when compiled with `USE_PTHREADS`
    - [x] UI for residue selection
    - [ ] select by clicking in WebGL
- [x] Fix strange `This build does not incorporate the necessary CCP4 SRS library` error message.
- [ ] Fix 5oah Ribbons showing nothing
- [ ] Selections for ribbons/worms (currently does all atoms whatever the selection)
- [ ] Display maps
  - [x] Generate map from `PDB_REDO` MTZ.
  - [x] Generate map from local MTZ.
    - [x] Load with `FC_ALL`, `PHIC_ALL`
    - [ ] Determine correct F/PHI columns for auto loading.
  - [x] Delete/close a map!!
  - [ ] Fix grid position issues - not quite right place?, etc.
  - [ ] Generate map from `PDB/EBI` MTZ.
  - [ ] Generate map from `PDB/EBI` structure factors.
- [ ] Some MG stuff in WebGL react-bootstrap
  - [x] Get Smiles (This only does svg. Seems there are no force fields in `RDKit_minimal` at present.
  - [x] Get Monomer
  - [x] Vibrations
    - [x] Scale caclulated B-Factors as is done in MG.
    - [x] Make the structure selection a combo or radio buttons instead of check boxes.
    - [x] Experimental vs. Theoretical B-values
    - [x] Cross correlation plot
    - [x] List normal modes
    - [x] Animate normal modes
  - [x] Put ligand picture somewhere better (in another Main tab).
  - [x] Glycan viewer to graphics interactivity
  - [x] Background colour
  - [x] Generate Helices
  - [x] PDB search
  - [x] PDB-REDO integration
  - [ ] Screenshots
  - [ ] Lipid cartoons
  - [ ] Images/Legends
  - [ ] Perfect spheres
  - [ ] Scale bar
  - [ ] Prosmart (this is slightly complicated because of the subprocess model).
- [ ] Change use of XMLHttpRequest/FileReader in various places to fetch API (as in PDBSearch.js).
- [ ] Keep ligand SVGs for all loaded structures and delete when file closed.
- [ ] Check that this successfully compiles on Windows (needs *get_sources.bat* or *MSYS*)
- [ ] Modify all relevant `CMakeLists.txt` to use `-sNODERAWFS=1` and `-DNODERAWFS` for executables so that native filesystem is available.
### Done ✓

- [x] Create my first TODO.md  
- [x] Update README with all changes in README.md.
- [x] Build gesamt
  - [x] Build librvapi
- [x] Test gesamt
- [x] gesamt in web page
- [x] multiple gesamt in web page
- [x] Make a fancier (React/Bootstrap) superpose/gesamt web page
- [x] Apply gesamt superposition to graphics
  - [x] Apply transformations
  - [x] Apply to newly created objects
  - [x] Undo button
- [x] Charged ligands produce SVG (e.g DCB 4a3h)
- [x] Check that this successfully compiles on Linux
- [x] Fix custom colours not working: `mgWebGLAtomsToPrimitives.js:5815 DON'T KNOW WHAT TO DO WITH #cf5353`
- [x] Move all computation into `crystallography_worker.js`.
  - [x] Mini-rsr
- [x] Flip peptide
- [x] Density fit validation tool (clickable)
  - [x] Create generic res vs. data widget
  - [x] Clickable
  - [x] Create simpler e.g. Bval vs residue widget
  - [x] Get density for per residue data
- [x] Ramachandran Plot
  - [x] Basic plot
  - [x] Feedback from hover/click
