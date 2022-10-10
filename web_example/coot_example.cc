/*
 * Copyright 2013 The Emscripten Authors.  All rights reserved.
 * Emscripten is available under two separate licenses, the MIT license and the
 * University of Illinois/NCSA Open Source License.  Both these licenses can be
 * found in the LICENSE file.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <iostream>
#include <string>
#include <vector>

#include <math.h>
#ifndef M_PI
#define M_PI           3.14159265358979323846
#endif

#include <emscripten.h>
#include <emscripten/bind.h>

#include "geometry/residue-and-atom-specs.hh"
#include "ligand/chi-angles.hh"
#include "ligand/primitive-chi-angles.hh"
#include "ligand/rotamer.hh"
#include "api/interfaces.hh"
#include "api/molecules_container.hh"
#include "api/validation-information.hh"
#include "api/g_triangle.hh"
#include "api/vertex.hh"

#include "mmdb_manager.h"
#include "clipper/core/ramachandran.h"
#include "clipper/clipper-ccp4.h"

#include "cartesian.h"
#include "geomutil.h"

int mini_rsr_main(int argc, char **argv);

using namespace emscripten;

extern void clear_getopt_initialized();

struct RamachandranInfo {
    std::string chainId;
    int seqNum;
    std::string insCode;
    std::string restype;
    double phi;
    double psi;
    bool isOutlier;
    bool is_pre_pro;
};

struct ResiduePropertyInfo {
    std::string chainId;
    int seqNum;
    std::string insCode;
    std::string restype;
    double property;
};

std::vector<ResiduePropertyInfo> getBVals(const std::string &pdbin, const std::string &chainId){
    std::vector<ResiduePropertyInfo> info;
    const char *filename_cp = pdbin.c_str();
    mmdb::InitMatType();
    mmdb::Manager *molHnd = new mmdb::Manager();

    int RC = molHnd->ReadCoorFile(filename_cp);
    int selHnd = molHnd->NewSelection();
    std::string selStr = std::string("/*/")+chainId+std::string("/(GLY,ALA,VAL,PRO,SER,THR,LEU,ILE,CYS,ASP,GLU,ASN,GLN,ARG,LYS,MET,MSE,HIS,PHE,TYR,TRP,HCS,ALO,PDD,UNK)");
    const char *sel_cp = selStr.c_str();
    molHnd->Select(selHnd,mmdb::STYPE_RESIDUE,sel_cp,mmdb::SKEY_NEW);
    mmdb::Residue** SelRes=0;
    int nRes;
    molHnd->GetSelIndex(selHnd,SelRes,nRes);
    for(int ires=0;ires<nRes;ires++){
        mmdb::Atom *N = SelRes[ires]->GetAtom(" N");
        mmdb::Atom *CA = SelRes[ires]->GetAtom("CA");
        mmdb::Atom *C = SelRes[ires]->GetAtom(" C");
        if(N&&CA&&C){
            ResiduePropertyInfo resInfo;
            resInfo.chainId = chainId;
            resInfo.seqNum = N->GetSeqNum();
            resInfo.insCode = std::string(N->GetInsCode());
            resInfo.restype = std::string(N->GetResidue()->GetResName());
            resInfo.property = CA->tempFactor;
            info.push_back(resInfo);
        }
    }

    return info;
}

std::vector<coot::residue_spec_t> getResidueSpecListForChain(const std::string &pdbin, const std::string &chainId_in){

    std::vector<coot::residue_spec_t> resSpecList;

    const char *filename_cp = pdbin.c_str();
    mmdb::InitMatType();
    mmdb::Manager *molHnd = new mmdb::Manager();
    int RC = molHnd->ReadCoorFile(filename_cp);

    mmdb::Model *model_p = molHnd->GetModel(1);
    int nchains = model_p->GetNumberOfChains();
    for (int ichain=0; ichain<nchains; ichain++) {
        mmdb::Chain *chain_p = model_p->GetChain(ichain);
        std::string chain_id = chain_p->GetChainID();
        if (chain_id == chainId_in) {
            int nres = chain_p->GetNumberOfResidues();
            mmdb::Residue *residue_p;
            for (int ires=0; ires<nres; ires++) { 
                residue_p = chain_p->GetResidue(ires);
                int resno = residue_p->GetSeqNum();
                std::string res_name(residue_p->GetResName());
                coot::residue_spec_t res_spec;
                res_spec.model_number = 1;
                res_spec.chain_id = chain_id;
                res_spec.res_no = residue_p->GetSeqNum();
                res_spec.ins_code = residue_p->GetInsCode();
                resSpecList.push_back(res_spec);
            }
        }
    }

    return resSpecList;

}

std::vector<std::string> getResidueListForChain(const std::string &pdbin, const std::string &chainId_in){

    std::vector<std::string> resList;

    const char *filename_cp = pdbin.c_str();
    mmdb::InitMatType();
    mmdb::Manager *molHnd = new mmdb::Manager();
    int RC = molHnd->ReadCoorFile(filename_cp);

    mmdb::Model *model_p = molHnd->GetModel(1);
    int nchains = model_p->GetNumberOfChains();
    for (int ichain=0; ichain<nchains; ichain++) {
        mmdb::Chain *chain_p = model_p->GetChain(ichain);
        std::string chain_id = chain_p->GetChainID();
        if (chain_id == chainId_in) {
            int nres = chain_p->GetNumberOfResidues();
            mmdb::Residue *residue_p;
            for (int ires=0; ires<nres; ires++) { 
                residue_p = chain_p->GetResidue(ires);
                int resno = residue_p->GetSeqNum();
                std::string res_name(residue_p->GetResName());
                resList.push_back(res_name);
            }
        }
    }

    return resList;

}

std::map<std::string,std::vector<coot::simple_rotamer> > getRotamersMap(){

    std::map<std::string,std::vector<coot::simple_rotamer> > all_rots;

    std::vector<std::string> rotamerAcids = {"VAL","PRO","SER","THR","LEU","ILE","CYS","ASP","GLU","ASN","GLN","ARG","LYS","MET","MSE","HIS","PHE","TYR","TRP"};

    coot::rotamer rot(0);

    for(unsigned ia=0;ia<rotamerAcids.size();ia++){
        std::vector<coot::simple_rotamer> rots =  rot.get_rotamers(rotamerAcids[ia], 0.001);
        all_rots[rotamerAcids[ia]] = rots;
    }

    return all_rots;
}

std::vector<RamachandranInfo> getRamachandranData(const std::string &pdbin, const std::string &chainId){
    std::cout << "std::vector<RamachandranInfo> getRamachandranData" << std::endl;
    std::vector<RamachandranInfo> info;
    const char *filename_cp = pdbin.c_str();
    mmdb::InitMatType();
    mmdb::Manager *molHnd = new mmdb::Manager();

    int RC = molHnd->ReadCoorFile(filename_cp);
    int selHnd = molHnd->NewSelection();
    std::string selStr = std::string("/*/")+chainId+std::string("/(GLY,ALA,VAL,PRO,SER,THR,LEU,ILE,CYS,ASP,GLU,ASN,GLN,ARG,LYS,MET,MSE,HIS,PHE,TYR,TRP,HCS,ALO,PDD,UNK)");
    const char *sel_cp = selStr.c_str();
    molHnd->Select(selHnd,mmdb::STYPE_RESIDUE,sel_cp,mmdb::SKEY_NEW);
    mmdb::Residue** SelRes=0;
    int nRes;
    molHnd->GetSelIndex(selHnd,SelRes,nRes);


    clipper::Ramachandran rama;
    clipper::Ramachandran r_gly, r_pro, r_non_gly_pro;
    clipper::Ramachandran r_ileval, r_pre_pro, r_non_gly_pro_pre_pro_ileval;
    rama.init(clipper::Ramachandran::All2);

    // Lovell et al. 2003, 50, 437 Protein Structure, Function and Genetics values:
    double rama_threshold_preferred = 0.02; 
    double rama_threshold_allowed = 0.002;
    float level_prefered = 0.02;
    float level_allowed = 0.002;

    //clipper defaults: 0.01 0.0005

    rama.set_thresholds(level_prefered, level_allowed);
    //
    r_gly.init(clipper::Ramachandran::Gly2);
    r_gly.set_thresholds(level_prefered, level_allowed);
    //
    r_pro.init(clipper::Ramachandran::Pro2);
    r_pro.set_thresholds(level_prefered, level_allowed);
    // first approximation; shouldnt be used if top8000 is available anyway
    r_non_gly_pro.init(clipper::Ramachandran::NoGPIVpreP2);
    r_non_gly_pro.set_thresholds(level_prefered, level_allowed);
    // new
    r_ileval.init(clipper::Ramachandran::IleVal2);
    r_ileval.set_thresholds(level_prefered, level_allowed);
    //
    r_pre_pro.init(clipper::Ramachandran::PrePro2);
    r_pre_pro.set_thresholds(level_prefered, level_allowed);
    //
    r_non_gly_pro_pre_pro_ileval.init(clipper::Ramachandran::NoGPIVpreP2);
    r_non_gly_pro_pre_pro_ileval.set_thresholds(level_prefered, level_allowed);


    //std::cout << nRes << " residues" << std::endl;

    for(int ires=1;ires<nRes-1;ires++){
        mmdb::Atom *N = SelRes[ires]->GetAtom(" N");
        mmdb::Atom *CA = SelRes[ires]->GetAtom("CA");
        mmdb::Atom *C = SelRes[ires]->GetAtom(" C");
        mmdb::Atom *Cm = SelRes[ires-1]->GetAtom(" C");
        mmdb::Atom *Np = SelRes[ires+1]->GetAtom(" N");
        if(N&&CA&&C&&Cm&&Np){
            RamachandranInfo resInfo;
            resInfo.chainId = chainId;
            resInfo.seqNum = N->GetSeqNum();
            resInfo.insCode = std::string(N->GetInsCode());
            resInfo.restype = std::string(N->GetResidue()->GetResName());
            std::string restypeP = std::string(Np->GetResidue()->GetResName());
            resInfo.is_pre_pro = false;
            if(restypeP=="PRO") resInfo.is_pre_pro = true;
            //Phi: C-N-CA-C
            //Psi: N-CA-C-N
            Cartesian Ncart(N->x,N->y,N->z);
            Cartesian CAcart(CA->x,CA->y,CA->z);
            Cartesian Ccart(C->x,C->y,C->z);
            Cartesian CMcart(Cm->x,Cm->y,Cm->z);
            Cartesian NPcart(Np->x,Np->y,Np->z);
            double phi = DihedralAngle(CMcart,Ncart,CAcart,Ccart);
            double psi = DihedralAngle(Ncart,CAcart,Ccart,NPcart);
            //std::cout << N->GetSeqNum() << " " << N->name << " " << CA->name << " " << C->name << " " << Cm->name << " " << Np->name << " " << phi*180.0/M_PI << " " << psi*180.0/M_PI << std::endl;
            resInfo.phi = phi*180.0/M_PI;
            resInfo.psi = psi*180.0/M_PI;
            bool r = false; //isOutlier
            if (resInfo.restype == "GLY") {
                if (! r_gly.allowed(phi, psi))
                    if (! r_gly.favored(phi, psi))
                        r = true;
            } else {
                if (resInfo.restype == "PRO") {
                    if (! r_pro.allowed(phi, psi))
                        if (! r_pro.favored(phi, psi))
                            r = true;
                } else {
                    if (resInfo.is_pre_pro) {
                        if (! r_pre_pro.allowed(phi, psi))
                            if (! r_pre_pro.favored(phi, psi))
                                r = true;
                    } else {
                        if ((resInfo.restype == "ILE") ||
                                (resInfo.restype == "VAL")) {
                            if (! r_ileval.allowed(phi, psi))
                                if (! r_ileval.favored(phi, psi))
                                    r = true;
                        } else {
                            if (! rama.allowed(phi, psi))
                                if (! rama.favored(phi, psi))
                                    r = true;
                        }
                    }
                }
            }
            resInfo.isOutlier = r;

            info.push_back(resInfo);
        }
    }

    return info;
}

/*
int flipPeptide(const std::string &pdbin, const std::string &chainId, const int resno, const std::string &pdbout){
    int retval = 0;
    std::cout << "In flipPeptide in C++. This does nothing useful." << std::endl;
    std::cout << "PDBIN: " << pdbin << std::endl;
    std::cout << "CHAIN: " << chainId << std::endl;
    std::cout << "RESNO: " << resno << std::endl;
    std::cout << "PDBOUT: " << pdbout << std::endl;

    const char *filename_cp = pdbin.c_str();
    const char *filename_out_cp = pdbout.c_str();

    //TODO - So this is where we should implement/call a proper function.
    //BEGIN STUB
    mmdb::InitMatType();
    mmdb::Manager *molHnd = new mmdb::Manager();

    int RC = molHnd->ReadCoorFile(filename_cp);
    assert(RC==0);

    RC = molHnd->WritePDBASCII(filename_out_cp);
    assert(RC==0);
    //END STUB

    return retval;
}
*/

int mini_rsr(const std::vector<std::string> &args){

    int argc = args.size();
    char **argv = new char*[argc];

    clear_getopt_initialized();

    for(int i=0;i<argc;i++){
        argv[i] = new char[args[i].size()+1];
        const char* arg_c = args[i].c_str();
        strcpy(argv[i], (char*)arg_c);
    }

    int retval = mini_rsr_main(argc,argv);

    for(int i=0;i<argc;i++){
        delete [] argv[i];
    }
    delete [] argv;

    return retval;
}

class molecules_container_js : public molecules_container_t {
    public:
        int writePDBASCII(int imol, const std::string &file_name) { 
            const char *fname_cp = file_name.c_str();
            return mol(imol)->WritePDBASCII(fname_cp);
        }
        int writeCCP4Map(int imol, const std::string &file_name) {
            auto xMap = (*this)[imol].xmap;
            auto clipperMap = clipper::CCP4MAPfile();
            clipperMap.open_write(file_name);
            clipperMap.export_xmap(xMap);
            return 0;
        }        
        int count_simple_mesh_vertices(const coot::simple_mesh_t &m) { return m.vertices.size(); }
};

std::string GetAtomNameFromAtom(mmdb::Atom *atom){
    return std::string(atom->GetAtomName());
}

std::string GetChainIDFromAtom(mmdb::Atom *atom){
    return std::string(atom->GetChainID());
}

std::string GetLabelAsymIDFromAtom(mmdb::Atom *atom){
    return std::string(atom->GetLabelAsymID());
}

std::string GetLabelCompIDFromAtom(mmdb::Atom *atom){
    return std::string(atom->GetLabelCompID());
}

std::string GetInsCodeFromAtom(mmdb::Atom *atom){
    return std::string(atom->GetInsCode());
}

std::string GetResNameFromResidue(mmdb::Residue *res){
    return std::string(res->GetResName());
}

std::string GetChainIDFromResidue(mmdb::Residue *res){
    return std::string(res->GetChainID());
}

std::string GetLabelAsymIDFromResidue(mmdb::Residue *res){
    return std::string(res->GetLabelAsymID());
}

std::string GetLabelCompIDFromResidue(mmdb::Residue *res){
    return std::string(res->GetLabelCompID());
}

std::string GetInsCodeFromResidue(mmdb::Residue *res){
    return std::string(res->GetInsCode());
}

EMSCRIPTEN_BINDINGS(my_module) {
    class_<clipper::Coord_orth>("Coord_orth")
    .constructor<const clipper::ftype&, const clipper::ftype&, const clipper::ftype&>()
    .function("x", &clipper::Coord_orth::x)
    .function("y", &clipper::Coord_orth::y)
    .function("z", &clipper::Coord_orth::z)
    ;
    class_<clipper::Cell_descr>("Cell_descr")
    .constructor<const clipper::ftype&, const clipper::ftype&, const clipper::ftype&, const clipper::ftype&, const clipper::ftype&, const clipper::ftype&>()
    .function("a", &clipper::Cell_descr::a)
    .function("b", &clipper::Cell_descr::b)
    .function("c", &clipper::Cell_descr::c)
    .function("alpha", &clipper::Cell_descr::alpha)
    .function("beta", &clipper::Cell_descr::beta)
    .function("gamma", &clipper::Cell_descr::gamma)
    .function("alpha_deg", &clipper::Cell_descr::alpha_deg)
    .function("beta_deg", &clipper::Cell_descr::beta_deg)
    .function("gamma_deg", &clipper::Cell_descr::gamma_deg)
    .function("format", &clipper::Cell_descr::format)
    ;
    class_<clipper::Cell, base<clipper::Cell_descr>>("Cell")
    .constructor()
    .constructor<const clipper::Cell_descr &>()
    .function("a_star", &clipper::Cell::a_star)
    .function("b_star", &clipper::Cell::b_star)
    .function("c_star", &clipper::Cell::c_star)
    .function("alpha_star", &clipper::Cell::alpha_star)
    .function("beta_star", &clipper::Cell::beta_star)
    .function("gamma_star", &clipper::Cell::gamma_star)
    .function("descr", &clipper::Cell::descr)
    .function("is_null", &clipper::Cell::is_null)
    .function("init", &clipper::Cell::init)
    ;
    class_<clipper::Xmap_base>("Xmap_base")
    .function("cell", &clipper::Xmap_base::cell)
    ;
    class_<clipper::String>("Clipper_String")
    .constructor()
    .constructor<const std::string>()
    ;
    class_<clipper::Xmap<float>, base<clipper::Xmap_base>>("Xmap_float")
    .constructor()
    ;
    class_<clipper::CCP4MAPfile>("CCP4MAPfile")
    .constructor()
    .function("open_read",&clipper::CCP4MAPfile::open_read)
    .function("open_write",&clipper::CCP4MAPfile::open_write)
    .function("close_read",&clipper::CCP4MAPfile::close_read)
    .function("close_write",&clipper::CCP4MAPfile::close_write)
    ;
    class_<coot::simple_rotamer>("simple_rotamer")
    .function("P_r1234",&coot::simple_rotamer::P_r1234)
    .function("Probability_rich",&coot::simple_rotamer::Probability_rich)
    .function("get_chi",&coot::simple_rotamer::get_chi)
    ;
    class_<coot::residue_validation_information_t>("residue_validation_information_t")
    .property("distortion", &coot::residue_validation_information_t::distortion)
    .property("label", &coot::residue_validation_information_t::label)
    .property("residue_spec", &coot::residue_validation_information_t::residue_spec)
    .property("atom_spec", &coot::residue_validation_information_t::atom_spec)
    ;
    class_<coot::chain_validation_information_t>("chain_validation_information_t")
    .property("name", &coot::chain_validation_information_t::name)
    .property("type", &coot::chain_validation_information_t::type)
    .property("chain_id", &coot::chain_validation_information_t::chain_id)
    .property("rviv", &coot::chain_validation_information_t::rviv)
    ;
    class_<coot::validation_information_t>("validation_information_t")
    .property("name", &coot::validation_information_t::name)
    .property("type", &coot::validation_information_t::type)
    .property("cviv", &coot::validation_information_t::cviv)
    .function("get_index_for_chain",&coot::validation_information_t::get_index_for_chain)
    ;
    class_<mmdb::Atom>("Atom")
    .constructor<>()
    .property("x",&mmdb::Atom::x)
    .property("y",&mmdb::Atom::y)
    .property("z",&mmdb::Atom::z)
    .property("serNum",&mmdb::Atom::serNum)
    .property("occupancy",&mmdb::Atom::occupancy)
    .property("tempFactor",&mmdb::Atom::tempFactor)
    .property("charge",&mmdb::Atom::charge)
    .property("sigX",&mmdb::Atom::sigX)
    .property("sigY",&mmdb::Atom::sigY)
    .property("sigZ",&mmdb::Atom::sigZ)
    .property("sigOcc",&mmdb::Atom::sigOcc)
    .property("sigTemp",&mmdb::Atom::sigTemp)
    .property("u11",&mmdb::Atom::u11)
    .property("u22",&mmdb::Atom::u22)
    .property("u33",&mmdb::Atom::u33)
    .property("u12",&mmdb::Atom::u12)
    .property("u13",&mmdb::Atom::u13)
    .property("u23",&mmdb::Atom::u23)
    .property("Het",&mmdb::Atom::Het)
    .property("Ter",&mmdb::Atom::Ter)
    .function("GetNBonds",&mmdb::Atom::GetNBonds)
    .function("GetModelNum",&mmdb::Atom::GetModelNum)
    .function("GetSeqNum",&mmdb::Atom::GetSeqNum)
    .function("GetLabelSeqID",&mmdb::Atom::GetLabelSeqID)
    .function("GetLabelEntityID",&mmdb::Atom::GetLabelEntityID)
    .function("GetSSEType",&mmdb::Atom::GetSSEType)
    .function("isTer",&mmdb::Atom::isTer)
    .function("isMetal",&mmdb::Atom::isMetal)
    .function("isSolvent",&mmdb::Atom::isSolvent)
    .function("isInSelection",&mmdb::Atom::isInSelection)
    .function("isNTerminus",&mmdb::Atom::isNTerminus)
    .function("isCTerminus",&mmdb::Atom::isCTerminus)
    .function("GetResidueNo",&mmdb::Atom::GetResidueNo)
    .function("GetIndex",&mmdb::Atom::GetIndex)
    .function("GetAtomName",&GetAtomNameFromAtom, allow_raw_pointers())
    .function("GetChainID",&GetChainIDFromAtom, allow_raw_pointers())
    .function("GetLabelAsymID",&GetLabelAsymIDFromAtom, allow_raw_pointers())
    .function("GetLabelCompID",&GetLabelCompIDFromAtom, allow_raw_pointers())
    .function("GetInsCode",&GetInsCodeFromAtom, allow_raw_pointers())
    ;
    class_<mmdb::Residue>("Residue")
    .constructor<>()
    .property("seqNum",&mmdb::Residue::seqNum)
    .property("label_seq_id",&mmdb::Residue::label_seq_id)
    .property("label_entity_id",&mmdb::Residue::label_entity_id)
    .property("index",&mmdb::Residue::index)
    .property("nAtoms",&mmdb::Residue::nAtoms)
    .function("GetModelNum",&mmdb::Residue::GetModelNum)
    .function("GetSeqNum",&mmdb::Residue::GetSeqNum)
    .function("GetLabelSeqID",&mmdb::Residue::GetLabelSeqID)
    .function("GetLabelEntityID",&mmdb::Residue::GetLabelEntityID)
    .function("GetResidueNo",&mmdb::Residue::GetResidueNo)
    .function("GetNofAltLocations",&mmdb::Residue::GetNofAltLocations)
    .function("isAminoacid",&mmdb::Residue::isAminoacid)
    .function("isNucleotide",&mmdb::Residue::isNucleotide)
    .function("isDNARNA",&mmdb::Residue::isDNARNA)
    .function("isSugar",&mmdb::Residue::isSugar)
    .function("isSolvent",&mmdb::Residue::isSolvent)
    .function("isModRes",&mmdb::Residue::isModRes)
    .function("isInSelection",&mmdb::Residue::isInSelection)
    .function("isNTerminus",&mmdb::Residue::isNTerminus)
    .function("isCTerminus",&mmdb::Residue::isCTerminus)
    .function("GetResName",&GetResNameFromResidue, allow_raw_pointers())
    .function("GetChainID",&GetChainIDFromResidue, allow_raw_pointers())
    .function("GetLabelAsymID",&GetLabelAsymIDFromResidue, allow_raw_pointers())
    .function("GetLabelCompID",&GetLabelCompIDFromResidue, allow_raw_pointers())
    .function("GetInsCode",&GetInsCodeFromResidue, allow_raw_pointers())
    .function("GetAtom", select_overload<mmdb::Atom*(int)>(&mmdb::Residue::GetAtom), allow_raw_pointers())
    .function("GetNumberOfAtoms", select_overload<int(void)>(&mmdb::Residue::GetNumberOfAtoms))
    .function("GetNumberOfAtoms_countTers", select_overload<int(bool)>(&mmdb::Residue::GetNumberOfAtoms))
    ;
    class_<molecules_container_t>("molecules_container_t")
    .constructor<>()
    .function("is_valid_model_molecule",&molecules_container_t::is_valid_model_molecule)
    .function("is_valid_map_molecule",&molecules_container_t::is_valid_map_molecule)
    .function("read_pdb",&molecules_container_t::read_pdb)
    .function("read_mtz",&molecules_container_t::read_mtz)
    .function("density_fit_analysis",&molecules_container_t::density_fit_analysis)
    //Using allow_raw_pointers(). Perhaps suggests we need to do something different from exposing mmdb pointers to JS.
    .function("get_residue",&molecules_container_t::get_residue, allow_raw_pointers())
    .function("get_atom",&molecules_container_t::get_atom, allow_raw_pointers())
    .function("flipPeptide_cid", select_overload<int(int, const std::string&,const std::string&)>(&molecules_container_t::flipPeptide))
    .function("flipPeptide_rs", select_overload<int(int, const coot::residue_spec_t&,const std::string&)>(&molecules_container_t::flipPeptide))
    .function("test_origin_cube",&molecules_container_t::test_origin_cube)
    .function("ramachandran_validation_markup_mesh",&molecules_container_t::ramachandran_validation_markup_mesh)
    .function("get_map_contours_mesh",&molecules_container_t::get_map_contours_mesh)
    ;
    class_<molecules_container_js, base<molecules_container_t>>("molecules_container_js")
    .constructor<>()
    .function("writePDBASCII",&molecules_container_js::writePDBASCII)
    .function("writeCCP4Map",&molecules_container_js::writeCCP4Map)
    .function("count_simple_mesh_vertices",&molecules_container_js::count_simple_mesh_vertices)
    ;
    class_<RamachandranInfo>("RamachandranInfo")
    .constructor<>()
    .property("chainId", &RamachandranInfo::chainId)
    .property("seqNum", &RamachandranInfo::seqNum)
    .property("insCode", &RamachandranInfo::insCode)
    .property("restype", &RamachandranInfo::restype)
    .property("phi", &RamachandranInfo::phi)
    .property("psi", &RamachandranInfo::psi)
    .property("isOutlier", &RamachandranInfo::isOutlier)
    .property("is_pre_pro", &RamachandranInfo::is_pre_pro)
    ;
    class_<ResiduePropertyInfo>("ResiduePropertyInfo")
    .constructor<>()
    .property("chainId", &ResiduePropertyInfo::chainId)
    .property("seqNum", &ResiduePropertyInfo::seqNum)
    .property("insCode", &ResiduePropertyInfo::insCode)
    .property("restype", &ResiduePropertyInfo::restype)
    .property("property", &ResiduePropertyInfo::property)
    ;
    class_<coot::atom_spec_t>("atom_spec_t")
    .constructor<const std::string &, int, const std::string &, const std::string &, const std::string &>()
    .property("chain_id",&coot::atom_spec_t::chain_id)
    .property("res_no",&coot::atom_spec_t::res_no)
    .property("ins_code",&coot::atom_spec_t::ins_code)
    .property("atom_name",&coot::atom_spec_t::atom_name)
    .property("alt_conf",&coot::atom_spec_t::alt_conf)
    .property("int_user_data",&coot::atom_spec_t::int_user_data)
    .property("float_user_data",&coot::atom_spec_t::float_user_data)
    .property("string_user_data",&coot::atom_spec_t::string_user_data)
    .property("model_number",&coot::atom_spec_t::model_number)
    ;
    class_<coot::residue_spec_t>("residue_spec_t")
    .constructor<const std::string &, int, const std::string &>()
    .property("model_number",&coot::residue_spec_t::model_number)
    .property("chain_id",&coot::residue_spec_t::chain_id)
    .property("res_no",&coot::residue_spec_t::res_no)
    .property("ins_code",&coot::residue_spec_t::ins_code)
    .property("int_user_data",&coot::residue_spec_t::int_user_data)
    ;
    class_<coot::api::vnc_vertex>("vnc_vertex")
    .constructor<const glm::vec3 &, const glm::vec3 &, const glm::vec4 &>()
    .property("pos",&coot::api::vnc_vertex::pos)
    .property("normal",&coot::api::vnc_vertex::normal)
    .property("color",&coot::api::vnc_vertex::color)
    ;
    value_object<g_triangle>("g_triangle")
    .field("point_id", &g_triangle::point_id)
    ;
    class_<coot::simple_mesh_t>("simple_mesh_t")
    .property("vertices",&coot::simple_mesh_t::vertices)
    .property("triangles",&coot::simple_mesh_t::triangles)
    ;
    register_vector<std::string>("VectorString");
    register_vector<RamachandranInfo>("VectorResidueIdentifier");
    register_vector<ResiduePropertyInfo>("VectorResiduePropertyInfo");
    register_vector<coot::chain_validation_information_t>("Vectorchain_validation_information_t");
    register_vector<coot::residue_validation_information_t>("Vectorresidue_validation_information_t");
    register_vector<coot::simple_rotamer>("Vectorsimple_rotamer");
    register_vector<coot::residue_spec_t>("Vectorresidue_spec_t");
    register_vector<coot::api::vnc_vertex>("Vectorvnc_veertex");
    register_vector<g_triangle>("Vectorg_triangle");
    register_map<std::string,std::vector<coot::simple_rotamer> >("MapStringVectorsimple_rotamer");
    value_array<glm::vec3>("array_float_3")
        .element(emscripten::index<0>())
        .element(emscripten::index<1>())
        .element(emscripten::index<2>())
    ;
    value_array<glm::vec4>("array_float_4")
        .element(emscripten::index<0>())
        .element(emscripten::index<1>())
        .element(emscripten::index<2>())
        .element(emscripten::index<3>())
    ;
    value_array<std::array<unsigned int, 3>>("array_unsigned_int_3")
        .element(emscripten::index<0>())
        .element(emscripten::index<1>())
        .element(emscripten::index<2>())
    ;
    function("mini_rsr",&mini_rsr);
    function("flipPeptide",&flipPeptide);
    function("getRamachandranData",&getRamachandranData);
    function("getRotamersMap",&getRotamersMap);
    function("getResidueListForChain",&getResidueListForChain);
    function("getResidueSpecListForChain",&getResidueSpecListForChain);
    function("getBVals",&getBVals);
}
