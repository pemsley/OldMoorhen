var CCP4Module;

importScripts('web_example.js');

createCCP4Module({print(t){postMessage(["output",t])},printErr(t){postMessage(["output",t]);}})
    .then(function(CCP4Mod) {
             CCP4Module = CCP4Mod;
            })
.catch((e) => {
        console.log("CCP4 problem :(");
        console.log(e);
        });

onmessage = function(e) {
    var arrayBuffer = e.data[0];
    const byteArray = new Uint8Array(arrayBuffer);
    var selectedFileName = e.data[1];

    let f_col = "FC";
    let phi_col = "PHIC";
    if(e.data[2]!==undefined && e.data[3]!==undefined){
        f_col = e.data[2];
        phi_col = e.data[3];
    }

    try {
        CCP4Module.FS_createDataFile(".", selectedFileName, byteArray, true, true);
    } catch(e) {
    }

    console.log("Send",selectedFileName,"to clipper");
    var result = CCP4Module.clipper_example_with_cols(selectedFileName,f_col,phi_col);
    console.log("In the worker...");
    console.log(result);
    console.log(result.cell());
    console.log(result.cell().a());
    console.log(result.cell().b());
    console.log(result.cell().c());
    console.log(result.cell().alpha());
    console.log(result.cell().beta());
    console.log(result.cell().gamma());
    CCP4Module.printMapStats(result);
    var fout = new CCP4Module.CCP4MAPfile();
    var outpath = new CCP4Module.Clipper_String("mapout.map");
    fout.open_write(outpath);
    CCP4Module.exportXMapToMapFile(fout,result);
    fout.close_write();
    console.log("Written",CCP4Module.clipperStringToString(outpath));
    var mapasstr = CCP4Module.FS.readFile(CCP4Module.clipperStringToString(outpath));
    postMessage(["result",mapasstr]);

}