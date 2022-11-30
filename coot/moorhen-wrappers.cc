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

#include <gemmi/span.hpp>
#include <gemmi/mmread.hpp>
#include <gemmi/gz.hpp>
#include <gemmi/model.hpp>

#define _SAJSON_H_INCLUDED_

using namespace emscripten;
using GemmiSMat33double = gemmi::SMat33<double>;
using GemmiSMat33float = gemmi::SMat33<float>;

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

std::string get_spacegroup_hm(const gemmi::SpaceGroup &sg){
    return std::string(sg.hm);
}
std::string get_spacegroup_hall(const gemmi::SpaceGroup &sg){
    return std::string(sg.hall);
}
std::string get_spacegroup_qualifier(const gemmi::SpaceGroup &sg){
    return std::string(sg.qualifier);
}
std::string get_element_name_as_string(const gemmi::Element &el){
    return std::string(el.name());
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

class IntVectorMergeMolInfoPair {
    public:
        int first;
        std::vector<merge_molecule_results_info_t> second;
};

class ResSpecStringPair {
    public:
        coot::residue_spec_t first;
        std::string second;
};

class molecules_container_js : public molecules_container_t {
    public:
        std::vector<float> getFloats(unsigned nFloats) { 
            std::vector<float> fs;
            for(unsigned i=0;i<nFloats;i++){
                fs.push_back(i*1.0);
            }
            return fs;
        }
        int add(int ic) { 
            return ic + 1;
        }
        int writePDBASCII(int imol, const std::string &file_name) { 
            const char *fname_cp = file_name.c_str();
            return get_mol(imol)->WritePDBASCII(fname_cp);
        }

        IntVectorMergeMolInfoPair merge_molecules(int imol, const std::string &list_of_other_molecules) {
            IntVectorMergeMolInfoPair retval;
            std::pair<int, std::vector<merge_molecule_results_info_t> > p = molecules_container_t::merge_molecules(imol,list_of_other_molecules);
            retval.first = p.first;
            retval.second = p.second;
            return retval;
        }

        std::vector<ResSpecStringPair> get_single_letter_codes_for_chain(int imol, const std::string &chain_id) {
            std::vector<ResSpecStringPair> retval;
            std::vector<std::pair<coot::residue_spec_t, std::string> > seq = molecules_container_t::get_single_letter_codes_for_chain(imol, chain_id);
            for(unsigned i=0;i<seq.size();i++){
                ResSpecStringPair p;
                p.first = seq[i].first;
                p.second = seq[i].second;
                retval.push_back(p);
            }
            return retval;
        }
        int writeCCP4Map(int imol, const std::string &file_name) {
            auto xMap = (*this)[imol].xmap;
            auto clipperMap = clipper::CCP4MAPfile();
            clipperMap.open_write(file_name);
            clipperMap.export_xmap(xMap);
            return 0;
        }        
        int count_simple_mesh_vertices(const coot::simple_mesh_t &m) { return m.vertices.size(); }
        std::vector<float> go_to_blob_array(float x1, float y1, float z1, float x2, float y2, float z2, float contour_level){
            std::vector<float> o;
            std::pair<bool, clipper::Coord_orth> pp = molecules_container_t::go_to_blob(x1, y1, z1, x2, y2, z2, contour_level);
            if(pp.first){
                o.push_back(pp.second.x());
                o.push_back(pp.second.y());
                o.push_back(pp.second.z());
            }
            return o;
        }
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
    class_<merge_molecule_results_info_t>("merge_molecule_results_info_t")
    .property("chain_id", &merge_molecule_results_info_t::chain_id)
    .property("spec", &merge_molecule_results_info_t::spec)
    .property("is_chain", &merge_molecule_results_info_t::is_chain)
    ;
    class_<coot::residue_validation_information_t>("residue_validation_information_t")
    .property("function_value", &coot::residue_validation_information_t::function_value)
    .property("label", &coot::residue_validation_information_t::label)
    .property("residue_spec", &coot::residue_validation_information_t::residue_spec)
    .property("atom_spec", &coot::residue_validation_information_t::atom_spec)
    ;
    class_<coot::chain_validation_information_t>("chain_validation_information_t")
    .property("name", &coot::chain_validation_information_t::name)
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
    class_<coot::phi_psi_prob_t>("phi_psi_prob_t")
    .property("phi_psi", &coot::phi_psi_prob_t::phi_psi)
    .property("position", &coot::phi_psi_prob_t::position)
    .property("is_allowed_flag", &coot::phi_psi_prob_t::is_allowed_flag)
    .property("residue_name", &coot::phi_psi_prob_t::residue_name)
    .function("is_allowed", &coot::phi_psi_prob_t::is_allowed)
    ;
    class_<coot::molecule_t::moved_atom_t>("moved_atom_t")
    .constructor<const std::string&, const std::string&, float, float, float, int>()
    .property("atom_name", &coot::molecule_t::moved_atom_t::atom_name)
    .property("alt_conf", &coot::molecule_t::moved_atom_t::alt_conf)
    .property("x", &coot::molecule_t::moved_atom_t::x)
    .property("y", &coot::molecule_t::moved_atom_t::y)
    .property("z", &coot::molecule_t::moved_atom_t::z)
    .property("index", &coot::molecule_t::moved_atom_t::index)
    ;
    class_<coot::molecule_t::interesting_place_t>("interesting_place_t")
    .constructor<const std::string &, const coot::residue_spec_t &, const clipper::Coord_orth &, const std::string &>()
    .constructor<const std::string &, const clipper::Coord_orth &, const std::string &>()
    .property("feature_type", &coot::molecule_t::interesting_place_t::feature_type)
    .property("residue_spec", &coot::molecule_t::interesting_place_t::residue_spec)
    .property("button_label", &coot::molecule_t::interesting_place_t::button_label)
    .property("feature_value", &coot::molecule_t::interesting_place_t::feature_value)
    .property("badness", &coot::molecule_t::interesting_place_t::badness)
    .property("x", &coot::molecule_t::interesting_place_t::x)
    .property("y", &coot::molecule_t::interesting_place_t::y)
    .property("z", &coot::molecule_t::interesting_place_t::z)
    ;
    class_<coot::molecule_t::moved_residue_t>("moved_residue_t")
    .constructor<const std::string&, int, const std::string&>()
    .property("chain_id", &coot::molecule_t::moved_residue_t::chain_id)
    .property("res_no", &coot::molecule_t::moved_residue_t::res_no)
    .property("ins_code", &coot::molecule_t::moved_residue_t::ins_code)
    .property("moved_atoms", &coot::molecule_t::moved_residue_t::moved_atoms)
    ;
    class_<molecules_container_t>("molecules_container_t")
    .constructor<>()
    .function("is_valid_model_molecule",&molecules_container_t::is_valid_model_molecule)
    .function("is_valid_map_molecule",&molecules_container_t::is_valid_map_molecule)
    .function("read_pdb",&molecules_container_t::read_pdb)
    .function("read_ccp4_map",&molecules_container_t::read_ccp4_map)
    .function("read_mtz",&molecules_container_t::read_mtz)
    //   int import_cif_dictionary(const std::string &cif_file_name, int imol_enc);
    .function("import_cif_dictionary",&molecules_container_t::import_cif_dictionary)
    .function("density_fit_analysis",&molecules_container_t::density_fit_analysis)
    //Using allow_raw_pointers(). Perhaps suggests we need to do something different from exposing mmdb pointers to JS.
    .function("get_residue",&molecules_container_t::get_residue, allow_raw_pointers())
    .function("get_atom",&molecules_container_t::get_atom, allow_raw_pointers())
    .function("flipPeptide_cid",   select_overload<int(int, const std::string&,      const std::string&)>(&molecules_container_t::flip_peptide_using_cid))
    .function("flipPeptide",       select_overload<int(int, const coot::atom_spec_t&,const std::string&)>(&molecules_container_t::flip_peptide))
    .function("side_chain_180",    select_overload<int(int, const std::string&)>                         (&molecules_container_t::side_chain_180))
    .function("eigen_flip_ligand", select_overload<void(int, const std::string&)>                        (&molecules_container_t::eigen_flip_ligand_using_cid))
    .function("jed_flip",          select_overload<std::string(int, const std::string&, bool)>           (&molecules_container_t::jed_flip))
    .function("add_terminal_residue_directly_using_cid", select_overload<int(int,  const std::string&)>(&molecules_container_t::add_terminal_residue_directly_using_cid))
    .function("test_origin_cube",&molecules_container_t::test_origin_cube)
    .function("ramachandran_validation_markup_mesh",&molecules_container_t::ramachandran_validation_markup_mesh)
    .function("get_rotamer_dodecs",&molecules_container_t::get_rotamer_dodecs)
    .function("auto_fit_rotamer",&molecules_container_t::auto_fit_rotamer)
    .function("get_map_contours_mesh",&molecules_container_t::get_map_contours_mesh)
    .function("geometry_init_standard",&molecules_container_t::geometry_init_standard)
    .function("fill_rotamer_probability_tables",&molecules_container_t::fill_rotamer_probability_tables)
    .function("copy_fragment_using_residue_range",&molecules_container_t::copy_fragment_using_residue_range)
    .function("copy_fragment_using_cid",&molecules_container_t::copy_fragment_using_cid)
    .function("close_molecule",&molecules_container_t::close_molecule)
    .function("undo",&molecules_container_t::undo)
    .function("redo",&molecules_container_t::redo)
    .function("refine_residues_using_atom_cid",&molecules_container_t::refine_residues_using_atom_cid)
    .function("refine_residue_range",&molecules_container_t::refine_residue_range)
    .function("set_imol_refinement_map",&molecules_container_t::set_imol_refinement_map)
    .function("mutate",&molecules_container_t::mutate)
    .function("delete_using_cid",&molecules_container_t::delete_using_cid)
    .function("get_bonds_mesh",&molecules_container_t::get_bonds_mesh)
    .function("go_to_blob",&molecules_container_t::go_to_blob)
    .function("set_map_sampling_rate",&molecules_container_t::set_map_sampling_rate)
    .function("get_monomer",&molecules_container_t::get_monomer)
    .function("get_monomer_and_position_at",&molecules_container_t::get_monomer_and_position_at)
    .function("move_molecule_to_new_centre",&molecules_container_t::move_molecule_to_new_centre)
    .function("apply_transformation_to_atom_selection",&molecules_container_t::apply_transformation_to_atom_selection)
    .function("new_positions_for_residue_atoms",&molecules_container_t::new_positions_for_residue_atoms)
    .function("new_positions_for_atoms_in_residues",&molecules_container_t::new_positions_for_atoms_in_residues)
    //.function("get_interesting_places",&molecules_container_t::get_interesting_places)
    .function("difference_map_peaks",&molecules_container_t::difference_map_peaks)
    .function("pepflips_using_difference_map",&molecules_container_t::pepflips_using_difference_map)
    .function("add_waters",&molecules_container_t::add_waters)
    .function("ramachandran_analysis",&molecules_container_t::ramachandran_analysis)
    .function("density_correlation_analysis",&molecules_container_t::density_correlation_analysis)
    .function("difference_map_peaks",&molecules_container_t::difference_map_peaks)
    .function("rotamer_analysis",&molecules_container_t::rotamer_analysis)
    .function("associate_data_mtz_file_with_map",&molecules_container_t::associate_data_mtz_file_with_map)
    .function("connect_updating_maps",&molecules_container_t::connect_updating_maps)
    .function("ramachandran_validation",&molecules_container_t::ramachandran_validation)
    ;
    class_<molecules_container_js, base<molecules_container_t>>("molecules_container_js")
    .constructor<>()
    .function("writePDBASCII",&molecules_container_js::writePDBASCII)
    .function("writeCCP4Map",&molecules_container_js::writeCCP4Map)
    .function("count_simple_mesh_vertices",&molecules_container_js::count_simple_mesh_vertices)
    .function("go_to_blob_array",&molecules_container_js::go_to_blob_array)
    .function("get_single_letter_codes_for_chain",&molecules_container_js::get_single_letter_codes_for_chain)
    .function("add",&molecules_container_js::add)
    .function("getFloats",&molecules_container_js::getFloats)
    .function("merge_molecules",&molecules_container_js::merge_molecules)
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
    class_<ResSpecStringPair>("ResSpecStringPair")
    .property("first",&ResSpecStringPair::first)
    .property("second",&ResSpecStringPair::second)
    ;
    class_<IntVectorMergeMolInfoPair>("IntVectorMergeMolInfoPair")
    .property("first",&IntVectorMergeMolInfoPair::first)
    .property("second",&IntVectorMergeMolInfoPair::second)
    ;
    class_<coot::util::phi_psi_t>("phi_psi_t")
    .function("phi",&coot::util::phi_psi_t::phi)
    .function("psi",&coot::util::phi_psi_t::psi)
    .function("label",&coot::util::phi_psi_t::label)
    .function("residue_name",&coot::util::phi_psi_t::residue_name)
    .function("is_filled",&coot::util::phi_psi_t::is_filled)
    .function("is_pre_pro",&coot::util::phi_psi_t::is_pre_pro)
    .property("ins_code",&coot::util::phi_psi_t::ins_code)
    .property("chain_id",&coot::util::phi_psi_t::chain_id)
    .property("residue_number",&coot::util::phi_psi_t::residue_number)
    ;
    class_<coot::Cartesian>("Cartesian")
    .function("x",&coot::Cartesian::x)
    .function("y",&coot::Cartesian::y)
    .function("z",&coot::Cartesian::z)
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
    register_vector<coot::molecule_t::moved_residue_t>("Vectormoved_residue_t");
    register_vector<coot::molecule_t::moved_atom_t>("Vectormoved_atom_t");
    register_vector<std::string>("VectorString");
    register_vector<float>("VectorFloat");
    register_vector<RamachandranInfo>("VectorResidueIdentifier");
    register_vector<ResiduePropertyInfo>("VectorResiduePropertyInfo");
    register_vector<coot::chain_validation_information_t>("Vectorchain_validation_information_t");
    register_vector<coot::residue_validation_information_t>("Vectorresidue_validation_information_t");
    register_vector<coot::simple_rotamer>("Vectorsimple_rotamer");
    register_vector<coot::residue_spec_t>("Vectorresidue_spec_t");
    register_vector<coot::api::vnc_vertex>("Vectorvnc_veertex");
    register_vector<coot::molecule_t::interesting_place_t>("Vectorinteresting_place_t");
    register_vector<g_triangle>("Vectorg_triangle");
    register_vector<ResSpecStringPair>("VectorResSpecStringPair");
    register_vector<merge_molecule_results_info_t>("Vectormerge_molecule_results_info_t");
    register_vector<coot::phi_psi_prob_t>("Vectophi_psi_prob_t");

    register_vector<gemmi::TlsGroup::Selection>("VectorGemmiTlsGroupSelection");
    register_vector<gemmi::TlsGroup>("VectorGemmiTlsGroup");
    register_vector<gemmi::RefinementInfo::Restr>("VectorGemmiRefinementInfoRestr");
    register_vector<gemmi::DiffractionInfo>("VectorGemmiDiffractionInfo");
    register_vector<gemmi::ReflectionsInfo>("VectorGemmiReflectionsInfo");
    register_vector<gemmi::BasicRefinementInfo>("VectorGemmiBasicRefinementInfo");
    register_vector<gemmi::ExperimentInfo>("VectorGemmiExperimentInfo");
    register_vector<gemmi::CrystalInfo>("VectorGemmiCrystalInfo");
    register_vector<gemmi::RefinementInfo>("VectorGemmiRefinementInfo");
    register_vector<gemmi::SoftwareItem>("VectorGemmiSoftwareItem");
    register_vector<gemmi::Assembly::Gen>("VectorGemmiAssemblyGen");
    register_vector<gemmi::Sheet::Strand>("VectorGemmiSheetStrand");
    register_vector<gemmi::Entity::DbRef>("VectorGemmiEntityDbRef");
    register_vector<gemmi::Atom>("VectorGemmiAtom");
    register_vector<gemmi::Model>("VectorGemmiModel");
    register_vector<gemmi::Op>("VectorGemmiOp");
    register_vector<gemmi::NcsOp>("VectorGemmiNcsOp");
    register_vector<gemmi::Entity>("VectorGemmiEntity");
    register_vector<gemmi::Connection>("VectorGemmiConnection");
    register_vector<gemmi::Helix>("VectorGemmiHelix");
    register_vector<gemmi::Sheet>("VectorGemmiSheet");
    register_vector<gemmi::Assembly>("VectorGemmiAssembly");
    register_vector<gemmi::Chain>("VectorGemmiChain");
    register_vector<gemmi::Residue>("VectorGemmiResidue");
    register_vector<gemmi::ResidueSpan>("VectorGemmiResidueSpan");
    register_vector<gemmi::ConstResidueSpan>("VectorGemmiConstResidueSpan");

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
    function("getRotamersMap",&getRotamersMap);

    enum_<gemmi::SoftwareItem::Classification>("Classification")
        .value("DataCollection", gemmi::SoftwareItem::Classification::DataCollection)
        .value("DataExtraction", gemmi::SoftwareItem::Classification::DataExtraction)
        .value("DataProcessing", gemmi::SoftwareItem::Classification::DataProcessing)
        .value("DataReduction", gemmi::SoftwareItem::Classification::DataReduction)
        .value("DataScaling", gemmi::SoftwareItem::Classification::DataScaling)
        .value("ModelBuilding", gemmi::SoftwareItem::Classification::ModelBuilding)
        .value("Phasing", gemmi::SoftwareItem::Classification::Phasing)
        .value("Refinement", gemmi::SoftwareItem::Classification::Refinement)
        .value("Unspecified", gemmi::SoftwareItem::Classification::Unspecified)
    ;

    enum_<gemmi::Assembly::SpecialKind>("SpecialKind")
        .value("NA", gemmi::Assembly::SpecialKind::NA)
        .value("CompleteIcosahedral", gemmi::Assembly::SpecialKind::CompleteIcosahedral)
        .value("RepresentativeHelical", gemmi::Assembly::SpecialKind::RepresentativeHelical)
        .value("CompletePoint", gemmi::Assembly::SpecialKind::CompletePoint)
    ;

    enum_<gemmi::Connection::Type>("ConnectionType")
        .value("Covale", gemmi::Connection::Type::Covale)
        .value("Disulf", gemmi::Connection::Type::Disulf)
        .value("Hydrog", gemmi::Connection::Type::Hydrog)
        .value("MetalC", gemmi::Connection::Type::MetalC)
        .value("Unknown", gemmi::Connection::Type::Unknown)
    ;

    enum_<gemmi::Helix::HelixClass>("HelixClass")
        .value("UnknownHelix", gemmi::Helix::HelixClass::UnknownHelix)
        .value("RAlpha", gemmi::Helix::HelixClass::RAlpha)
        .value("ROmega", gemmi::Helix::HelixClass::ROmega)
        .value("RPi", gemmi::Helix::HelixClass::RPi)
        .value("RGamma", gemmi::Helix::HelixClass::RGamma)
        .value("R310", gemmi::Helix::HelixClass::R310)
        .value("LAlpha", gemmi::Helix::HelixClass::LAlpha)
        .value("LOmega", gemmi::Helix::HelixClass::LOmega)
        .value("LGamma", gemmi::Helix::HelixClass::LGamma)
        .value("Helix27", gemmi::Helix::HelixClass::Helix27)
        .value("HelixPolyProlineNone", gemmi::Helix::HelixClass::HelixPolyProlineNone)
    ;

    enum_<gemmi::Asu>("Asu")
        .value("Same", gemmi::Asu::Same)
        .value("Different", gemmi::Asu::Different)
        .value("Any", gemmi::Asu::Any)
    ;

    enum_<gemmi::El>("El")
        .value("X", gemmi::El::X)
        .value("He", gemmi::El::He)
        .value("Li", gemmi::El::Li)
        .value("Be", gemmi::El::Be)
        .value("B", gemmi::El::B)
        .value("C", gemmi::El::C)
        .value("N", gemmi::El::N)
        .value("O", gemmi::El::O)
        .value("F", gemmi::El::F)
        .value("Ne", gemmi::El::Ne)
        .value("Na", gemmi::El::Na)
        .value("Mg", gemmi::El::Mg)
        .value("Al", gemmi::El::Al)
        .value("Si", gemmi::El::Si)
        .value("P", gemmi::El::P)
        .value("S", gemmi::El::S)
        .value("Cl", gemmi::El::Cl)
        .value("Ar", gemmi::El::Ar)
        .value("K", gemmi::El::K)
        .value("Ca", gemmi::El::Ca)
        .value("Sc", gemmi::El::Sc)
        .value("Ti", gemmi::El::Ti)
        .value("V", gemmi::El::V)
        .value("Cr", gemmi::El::Cr)
        .value("Mn", gemmi::El::Mn)
        .value("Fe", gemmi::El::Fe)
        .value("Co", gemmi::El::Co)
        .value("Ni", gemmi::El::Ni)
        .value("Cu", gemmi::El::Cu)
        .value("Zn", gemmi::El::Zn)
        .value("Ga", gemmi::El::Ga)
        .value("Ge", gemmi::El::Ge)
        .value("As", gemmi::El::As)
        .value("Se", gemmi::El::Se)
        .value("Br", gemmi::El::Br)
        .value("Kr", gemmi::El::Kr)
        .value("Rb", gemmi::El::Rb)
        .value("Sr", gemmi::El::Sr)
        .value("Y", gemmi::El::Y)
        .value("Zr", gemmi::El::Zr)
        .value("Nb", gemmi::El::Nb)
        .value("Mo", gemmi::El::Mo)
        .value("Tc", gemmi::El::Tc)
        .value("Ru", gemmi::El::Ru)
        .value("Rh", gemmi::El::Rh)
        .value("Pd", gemmi::El::Pd)
        .value("Ag", gemmi::El::Ag)
        .value("Cd", gemmi::El::Cd)
        .value("In", gemmi::El::In)
        .value("Sn", gemmi::El::Sn)
        .value("Sb", gemmi::El::Sb)
        .value("Te", gemmi::El::Te)
        .value("I", gemmi::El::I)
        .value("Xe", gemmi::El::Xe)
        .value("Cs", gemmi::El::Cs)
        .value("Ba", gemmi::El::Ba)
        .value("La", gemmi::El::La)
        .value("Ce", gemmi::El::Ce)
        .value("Pr", gemmi::El::Pr)
        .value("Nd", gemmi::El::Nd)
        .value("Pm", gemmi::El::Pm)
        .value("Sm", gemmi::El::Sm)
        .value("Eu", gemmi::El::Eu)
        .value("Gd", gemmi::El::Gd)
        .value("Tb", gemmi::El::Tb)
        .value("Dy", gemmi::El::Dy)
        .value("Ho", gemmi::El::Ho)
        .value("Er", gemmi::El::Er)
        .value("Tm", gemmi::El::Tm)
        .value("Yb", gemmi::El::Yb)
        .value("Lu", gemmi::El::Lu)
        .value("Hf", gemmi::El::Hf)
        .value("Ta", gemmi::El::Ta)
        .value("W", gemmi::El::W)
        .value("Re", gemmi::El::Re)
        .value("Os", gemmi::El::Os)
        .value("Ir", gemmi::El::Ir)
        .value("Pt", gemmi::El::Pt)
        .value("Au", gemmi::El::Au)
        .value("Hg", gemmi::El::Hg)
        .value("Tl", gemmi::El::Tl)
        .value("Pb", gemmi::El::Pb)
        .value("Bi", gemmi::El::Bi)
        .value("Po", gemmi::El::Po)
        .value("At", gemmi::El::At)
        .value("Rn", gemmi::El::Rn)
        .value("Fr", gemmi::El::Fr)
        .value("Ra", gemmi::El::Ra)
        .value("Ac", gemmi::El::Ac)
        .value("Th", gemmi::El::Th)
        .value("Pa", gemmi::El::Pa)
        .value("U", gemmi::El::U)
        .value("Np", gemmi::El::Np)
        .value("Pu", gemmi::El::Pu)
        .value("Am", gemmi::El::Am)
        .value("Cm", gemmi::El::Cm)
        .value("Bk", gemmi::El::Bk)
        .value("Cf", gemmi::El::Cf)
        .value("Es", gemmi::El::Es)
        .value("Fm", gemmi::El::Fm)
        .value("Md", gemmi::El::Md)
        .value("No", gemmi::El::No)
        .value("Lr", gemmi::El::Lr)
        .value("Rf", gemmi::El::Rf)
        .value("Db", gemmi::El::Db)
        .value("Sg", gemmi::El::Sg)
        .value("Bh", gemmi::El::Bh)
        .value("Hs", gemmi::El::Hs)
        .value("Mt", gemmi::El::Mt)
        .value("Ds", gemmi::El::Ds)
        .value("Rg", gemmi::El::Rg)
        .value("Cn", gemmi::El::Cn)
        .value("Nh", gemmi::El::Nh)
        .value("Fl", gemmi::El::Fl)
        .value("Mc", gemmi::El::Mc)
        .value("Lv", gemmi::El::Lv)
        .value("Ts", gemmi::El::Ts)
        .value("Og", gemmi::El::Og)
        .value("D", gemmi::El::D)
        .value("END", gemmi::El::END)
    ;

    enum_<gemmi::PolymerType>("PolymerType")
        .value("Unknown", gemmi::PolymerType::Unknown)
        .value("PeptideL", gemmi::PolymerType::PeptideL)
        .value("PeptideD", gemmi::PolymerType::PeptideD)
        .value("Dna", gemmi::PolymerType::Dna)
        .value("Rna", gemmi::PolymerType::Rna)
        .value("DnaRnaHybrid", gemmi::PolymerType::DnaRnaHybrid)
        .value("SaccharideD", gemmi::PolymerType::SaccharideD)
        .value("SaccharideL", gemmi::PolymerType::SaccharideL)
        .value("Pna", gemmi::PolymerType::Pna)
        .value("CyclicPseudoPeptide", gemmi::PolymerType::CyclicPseudoPeptide)
        .value("Other", gemmi::PolymerType::Other)
    ;

    enum_<gemmi::PointGroup>("PointGroup")
        .value("C1", gemmi::PointGroup::C1)
        .value("Ci", gemmi::PointGroup::Ci)
        .value("C2", gemmi::PointGroup::C2)
        .value("Cs", gemmi::PointGroup::Cs)
        .value("C2h", gemmi::PointGroup::C2h)
        .value("D2", gemmi::PointGroup::D2)
        .value("C2v", gemmi::PointGroup::C2v)
        .value("D2h", gemmi::PointGroup::D2h)
        .value("C4", gemmi::PointGroup::C4)
        .value("S4", gemmi::PointGroup::S4)
        .value("C4h", gemmi::PointGroup::C4h)
        .value("D4", gemmi::PointGroup::D4)
        .value("C4v", gemmi::PointGroup::C4v)
        .value("D2d", gemmi::PointGroup::D2d)
        .value("D4h", gemmi::PointGroup::D4h)
        .value("C3", gemmi::PointGroup::C3)
        .value("C3i", gemmi::PointGroup::C3i)
        .value("D3", gemmi::PointGroup::D3)
        .value("C3v", gemmi::PointGroup::C3v)
        .value("D3d", gemmi::PointGroup::D3d)
        .value("C6", gemmi::PointGroup::C6)
        .value("C3h", gemmi::PointGroup::C3h)
        .value("C6h", gemmi::PointGroup::C6h)
        .value("D6", gemmi::PointGroup::D6)
        .value("C6v", gemmi::PointGroup::C6v)
        .value("D3h", gemmi::PointGroup::D3h)
        .value("D6h", gemmi::PointGroup::D6h)
        .value("T", gemmi::PointGroup::T)
        .value("Th", gemmi::PointGroup::Th)
        .value("O", gemmi::PointGroup::O)
        .value("Td", gemmi::PointGroup::Td)
        .value("Oh", gemmi::PointGroup::Oh)
    ;

    enum_<gemmi::Laue>("Laue")
        .value("L1", gemmi::Laue::L1)
        .value("L2m", gemmi::Laue::L2m)
        .value("Lmmm", gemmi::Laue::Lmmm)
        .value("L4m", gemmi::Laue::L4m)
        .value("L4mmm", gemmi::Laue::L4mmm)
        .value("L3", gemmi::Laue::L3)
        .value("L3m", gemmi::Laue::L3m)
        .value("L6m", gemmi::Laue::L6m)
        .value("L6mmm", gemmi::Laue::L6mmm)
        .value("Lm3", gemmi::Laue::Lm3)
        .value("Lm3m", gemmi::Laue::Lm3m)
    ;

    enum_<gemmi::EntityType>("EntityType")
        .value("Unknown", gemmi::EntityType::Unknown)
        .value("Polymer", gemmi::EntityType::Polymer)
        .value("NonPolymer", gemmi::EntityType::NonPolymer)
        .value("Branched", gemmi::EntityType::Branched)
        .value("Water", gemmi::EntityType::Water)
    ;

    enum_<gemmi::CalcFlag>("CalcFlag")
        .value("NotSet", gemmi::CalcFlag::NotSet)
        .value("Determined", gemmi::CalcFlag::Determined)
        .value("Calculated", gemmi::CalcFlag::Calculated)
        .value("Dummy", gemmi::CalcFlag::Dummy)
    ;

    enum_<gemmi::CoorFormat>("CoorFormat")
        .value("Unknown", gemmi::CoorFormat::Unknown)
        .value("Detect", gemmi::CoorFormat::Detect)
        .value("Pdb", gemmi::CoorFormat::Pdb)
        .value("Mmcif", gemmi::CoorFormat::Mmcif)
        .value("Mmjson", gemmi::CoorFormat::Mmjson)
        .value("ChemComp", gemmi::CoorFormat::ChemComp)
    ;

    class_<gemmi::Span<const gemmi::Residue>>("SpanConstResidue")
    .function("front",select_overload<const gemmi::Residue&()const>(&gemmi::Span<const gemmi::Residue>::front))
    .function("back",select_overload<const gemmi::Residue&()const>(&gemmi::Span<const gemmi::Residue>::back))
    .function("at",select_overload<const gemmi::Residue&(std::size_t)const>(&gemmi::Span<const gemmi::Residue>::at))
    .function("size",&gemmi::Span<const gemmi::Residue>::size)
    .function("empty",&gemmi::Span<const gemmi::Residue>::empty)
    .function("children",select_overload<const gemmi::Span<const gemmi::Residue>&()const>(&gemmi::Span<const gemmi::Residue>::children))
    ;

    class_<gemmi::Span<gemmi::Residue>>("SpanResidue")
    .function("front",select_overload<gemmi::Residue&()>(&gemmi::Span<gemmi::Residue>::front))
    .function("back",select_overload<gemmi::Residue&()>(&gemmi::Span<gemmi::Residue>::back))
    .function("at",select_overload<gemmi::Residue&(std::size_t)>(&gemmi::Span<gemmi::Residue>::at))
    .function("size",&gemmi::Span<gemmi::Residue>::size)
    .function("set_size",&gemmi::Span<gemmi::Residue>::set_size)
    .function("empty",&gemmi::Span<gemmi::Residue>::empty)
    .function("children",select_overload<gemmi::Span<gemmi::Residue>&()>(&gemmi::Span<gemmi::Residue>::children))
    ;

    class_<gemmi::MutableVectorSpan<gemmi::Residue>, base<gemmi::Span<gemmi::Residue>>>("MutableVectorSpanResidue")
    .function("is_beginning",&gemmi::MutableVectorSpan<gemmi::Residue>::is_beginning)
    .function("is_ending",&gemmi::MutableVectorSpan<gemmi::Residue>::is_ending)
    ;

    class_<gemmi::UnitCell>("UnitCell")
    .constructor<>()
    .constructor<double,  double, double, double, double, double>()
    .property("a",&gemmi::UnitCell::a)
    .property("b",&gemmi::UnitCell::b)
    .property("c",&gemmi::UnitCell::c)
    .property("alpha",&gemmi::UnitCell::alpha)
    .property("beta",&gemmi::UnitCell::beta)
    .property("gamma",&gemmi::UnitCell::gamma)
    .property("orth",&gemmi::UnitCell::orth)
    .property("frac",&gemmi::UnitCell::frac)
    .property("volume",&gemmi::UnitCell::volume)
    .property("ar",&gemmi::UnitCell::ar)
    .property("br",&gemmi::UnitCell::br)
    .property("cr",&gemmi::UnitCell::cr)
    .property("cos_alphar",&gemmi::UnitCell::cos_alphar)
    .property("cos_betar",&gemmi::UnitCell::cos_betar)
    .property("cos_gammar",&gemmi::UnitCell::cos_gammar)
    .property("explicit_matrices",&gemmi::UnitCell::explicit_matrices)
    .property("cs_count",&gemmi::UnitCell::cs_count)
    .property("images",&gemmi::UnitCell::images)
    .function("is_crystal",&gemmi::UnitCell::is_crystal)
    .function("approx",&gemmi::UnitCell::approx)
    .function("is_similar",&gemmi::UnitCell::is_similar)
    .function("calculate_properties",&gemmi::UnitCell::calculate_properties)
    .function("cos_alpha",&gemmi::UnitCell::cos_alpha)
    .function("calculate_matrix_B",&gemmi::UnitCell::calculate_matrix_B)
    .function("set_matrices_from_fract",&gemmi::UnitCell::set_matrices_from_fract)
    .function("set",&gemmi::UnitCell::set)
    .function("set_from_vectors",&gemmi::UnitCell::set_from_vectors)
    .function("changed_basis_backward",&gemmi::UnitCell::changed_basis_backward)
    .function("changed_basis_forward",&gemmi::UnitCell::changed_basis_forward)
    .function("is_compatible_with_groupops",&gemmi::UnitCell::is_compatible_with_groupops)
    .function("add_ncs_images_to_cs_images",&gemmi::UnitCell::add_ncs_images_to_cs_images)
    .function("get_ncs_transforms",&gemmi::UnitCell::get_ncs_transforms)
    .function("orthogonalize",&gemmi::UnitCell::orthogonalize)
    .function("fractionalize",&gemmi::UnitCell::fractionalize)
    .function("orthogonalize_difference",&gemmi::UnitCell::orthogonalize_difference)
    .function("fractionalize_difference",&gemmi::UnitCell::fractionalize_difference)
    .function("op_as_transform",&gemmi::UnitCell::op_as_transform)
    .function("distance_sq_frac",select_overload<double(const gemmi::Fractional&,const gemmi::Fractional&)const>(&gemmi::UnitCell::distance_sq))
    .function("distance_sq_pos",select_overload<double(const gemmi::Position&,const gemmi::Position&)const>(&gemmi::UnitCell::distance_sq))
    .function("volume_per_image",&gemmi::UnitCell::volume_per_image)
    .function("search_pbc_images",&gemmi::UnitCell::search_pbc_images)
    .function("find_nearest_image",&gemmi::UnitCell::find_nearest_image)
    .function("apply_transform",&gemmi::UnitCell::apply_transform)
    .function("find_nearest_pbc_image_frac",select_overload<gemmi::NearestImage(const gemmi::Fractional&,gemmi::Fractional,int)const>(&gemmi::UnitCell::find_nearest_pbc_image))
    .function("find_nearest_pbc_image_pos",select_overload<gemmi::NearestImage(const gemmi::Position&,const gemmi::Position&,int)const>(&gemmi::UnitCell::find_nearest_pbc_image))
    .function("orthogonalize_in_pbc",&gemmi::UnitCell::orthogonalize_in_pbc)
    .function("find_nearest_pbc_position",&gemmi::UnitCell::find_nearest_pbc_position)
    .function("is_special_position_frac",select_overload<int(const gemmi::Fractional&,double)const>(&gemmi::UnitCell::is_special_position))
    .function("is_special_position_pos",select_overload<int(const gemmi::Position&,double)const>(&gemmi::UnitCell::is_special_position))
    .function("calculate_1_d2_double",&gemmi::UnitCell::calculate_1_d2_double)
    .function("calculate_1_d2",&gemmi::UnitCell::calculate_1_d2)
    .function("calculate_d",&gemmi::UnitCell::calculate_d)
    .function("calculate_stol_sq",&gemmi::UnitCell::calculate_stol_sq)
    .function("reciprocal",&gemmi::UnitCell::reciprocal)
    .function("get_hkl_limits",&gemmi::UnitCell::get_hkl_limits)
    .function("primitive_orth_matrix",&gemmi::UnitCell::primitive_orth_matrix)
    .function("calculate_u_eq",&gemmi::UnitCell::calculate_u_eq)
    .function("metric_tensor",&gemmi::UnitCell::metric_tensor)
    .function("reciprocal_metric_tensor",&gemmi::UnitCell::reciprocal_metric_tensor)
    .function("is_compatible_with_spacegroup",&gemmi::UnitCell::is_compatible_with_spacegroup, allow_raw_pointers())
    .function("set_cell_images_from_spacegroup",&gemmi::UnitCell::set_cell_images_from_spacegroup, allow_raw_pointers())
    ;

    class_<gemmi::Model>("Model")
    .property("name",&gemmi::Model::name)
    .property("chains",&gemmi::Model::chains)
    .function("remove_chain",&gemmi::Model::remove_chain)
    .function("merge_chain_parts",&gemmi::Model::merge_chain_parts)
    .function("get_subchain",select_overload<gemmi::ResidueSpan(const std::string&)>(&gemmi::Model::get_subchain))
    .function("get_subchain_const",select_overload<gemmi::ConstResidueSpan(const std::string&)const>(&gemmi::Model::get_subchain))
    .function("subchains",select_overload<std::vector<gemmi::ResidueSpan> ()>(&gemmi::Model::subchains))
    .function("subchains",select_overload<std::vector<gemmi::ConstResidueSpan> ()const>(&gemmi::Model::subchains))
    .function("get_all_residue_names",&gemmi::Model::get_all_residue_names)
    .function("find_residue_group",&gemmi::Model::find_residue_group)
    .function("sole_residue",&gemmi::Model::sole_residue)
    .function("find_cra",select_overload<gemmi::CRA(const gemmi::AtomAddress&, bool)>(&gemmi::Model::find_cra))
    .function("find_cra_const",select_overload<gemmi::const_CRA(const gemmi::AtomAddress&, bool)const>(&gemmi::Model::find_cra))
    .function("all",select_overload<gemmi::CraProxy()>(&gemmi::Model::all))
    .function("all_const",select_overload<gemmi::ConstCraProxy()const>(&gemmi::Model::all))
    .function("empty_copy",&gemmi::Model::empty_copy)
    .function("children",select_overload<std::vector<gemmi::Chain>&()>(&gemmi::Model::children))
    .function("children_const",select_overload<const std::vector<gemmi::Chain>&()const>(&gemmi::Model::children))
    ;

    class_<gemmi::Chain>("Chain")
    .property("name",&gemmi::Chain::name)
    .property("residues",&gemmi::Chain::residues)
    .property("empty_copy",&gemmi::Chain::empty_copy)
    .function("is_first_in_group",&gemmi::Chain::is_first_in_group)
    .function("whole_const",select_overload<gemmi::ConstResidueSpan()const>(&gemmi::Chain::whole))
    .function("get_polymer_const",select_overload<gemmi::ConstResidueSpan()const>(&gemmi::Chain::get_polymer))
    .function("get_ligands_const",select_overload<gemmi::ConstResidueSpan()const>(&gemmi::Chain::get_ligands))
    .function("get_waters_const",select_overload<gemmi::ConstResidueSpan()const>(&gemmi::Chain::get_waters))
    .function("get_subchain_const",select_overload<gemmi::ConstResidueSpan(const std::string&)const>(&gemmi::Chain::get_subchain))
    .function("subchains_const",select_overload<std::vector<gemmi::ConstResidueSpan> ()const>(&gemmi::Chain::subchains))
    .function("find_residue_group_const",select_overload<gemmi::ConstResidueGroup(gemmi::SeqId id)const>(&gemmi::Chain::find_residue_group))
    .function("children_const",select_overload<const std::vector<gemmi::Residue>&()const>(&gemmi::Chain::children))
    .function("whole",select_overload<gemmi::ResidueSpan()>(&gemmi::Chain::whole))
    .function("get_polymer",select_overload<gemmi::ResidueSpan()>(&gemmi::Chain::get_polymer))
    .function("get_ligands",select_overload<gemmi::ResidueSpan()>(&gemmi::Chain::get_ligands))
    .function("get_waters",select_overload<gemmi::ResidueSpan()>(&gemmi::Chain::get_waters))
    .function("get_subchain",select_overload<gemmi::ResidueSpan(const std::string&)>(&gemmi::Chain::get_subchain))
    .function("subchains",select_overload<std::vector<gemmi::ResidueSpan> ()>(&gemmi::Chain::subchains))
    .function("find_residue_group",select_overload<gemmi::ResidueGroup(gemmi::SeqId id)>(&gemmi::Chain::find_residue_group))
    .function("children",select_overload<std::vector<gemmi::Residue>&()>(&gemmi::Chain::children))
    //.function("first_conformer_const",select_overload<gemmi::ConstUniqProxy<gemmi::Residue>()const>(&gemmi::Chain::first_conformer))
    //.function("first_conformer",select_overload<gemmi::UniqProxy<gemmi::Residue>()>(&gemmi::Chain::first_conformer))
    //And various pointer return methods ...
    ;

    class_<gemmi::ConstResidueSpan, base<gemmi::Span<const gemmi::Residue>>>("ConstResidueSpan")
    .function("length",&gemmi::ConstResidueSpan::length)
    .function("subchain_id",&gemmi::ConstResidueSpan::subchain_id)
    .function("find_residue_group",&gemmi::ConstResidueSpan::find_residue_group)
    .function("extract_sequence",&gemmi::ConstResidueSpan::extract_sequence)
    .function("extreme_num",&gemmi::ConstResidueSpan::extreme_num)
    .function("label_seq_id_to_auth",&gemmi::ConstResidueSpan::label_seq_id_to_auth)
    .function("auth_seq_id_to_label",&gemmi::ConstResidueSpan::auth_seq_id_to_label)
    //ConstUniqProxy<Residue, ConstResidueSpan> first_conformer() const {
    //ConstUniqProxy<Residue, ConstResidueSpan> first_conformer() const {
    ;

    class_<gemmi::ResidueSpan, base<gemmi::MutableVectorSpan<gemmi::Residue>>>("ResidueSpan")
    .function("length",&gemmi::ResidueSpan::length)
    .function("subchain_id",&gemmi::ResidueSpan::subchain_id)
    .function("find_residue_group",select_overload<gemmi::ResidueGroup(gemmi::SeqId)>(&gemmi::ResidueSpan::find_residue_group))
    .function("find_residue_group_const",select_overload<gemmi::ConstResidueGroup(gemmi::SeqId)const>(&gemmi::ResidueSpan::find_residue_group))
    .function("extreme_num",&gemmi::ResidueSpan::extreme_num)
    .function("label_seq_id_to_auth",&gemmi::ResidueSpan::label_seq_id_to_auth)
    .function("auth_seq_id_to_label",&gemmi::ResidueSpan::auth_seq_id_to_label)
    ;

    class_<gemmi::ConstResidueGroup,base<gemmi::ConstResidueSpan>>("ConstResidueGroup")
    .function("by_resname",&gemmi::ConstResidueGroup::by_resname)
    ;

    class_<gemmi::ResidueGroup>("ResidueGroup")
    .function("by_resname",&gemmi::ResidueGroup::by_resname)
    .function("remove_residue",&gemmi::ResidueGroup::remove_residue)
    ;

    class_<gemmi::SeqId::OptionalNum>("OptionalNum")
    .property("value",&gemmi::SeqId::OptionalNum::value)
    .function("has_value",&gemmi::SeqId::OptionalNum::has_value)
    .function("str",&gemmi::SeqId::OptionalNum::str)
    ;

    class_<gemmi::SeqId>("SeqId")
    .property("num",&gemmi::SeqId::num)
    .property("icode",&gemmi::SeqId::icode)
    .function("has_icode",&gemmi::SeqId::has_icode)
    .function("str",&gemmi::SeqId::str)
    ;

    class_<gemmi::ResidueId>("ResidueId")
    .property("seqid",&gemmi::ResidueId::seqid)
    .property("segment",&gemmi::ResidueId::segment)
    .property("name",&gemmi::ResidueId::name)
    .function("group_key",&gemmi::ResidueId::group_key)
    .function("matches",&gemmi::ResidueId::matches)
    .function("matches_noseg",&gemmi::ResidueId::matches_noseg)
    ;

    class_<gemmi::Residue, base<gemmi::ResidueId>>("GemmiResidue")
    .property("subchain",&gemmi::Residue::subchain)
    .property("entity_id",&gemmi::Residue::entity_id)
    .property("label_seq",&gemmi::Residue::label_seq)
    .property("entity_type",&gemmi::Residue::entity_type)
    .property("het_flag",&gemmi::Residue::het_flag)
    .property("is_cis",&gemmi::Residue::is_cis)
    .property("flag",&gemmi::Residue::flag)
    .property("atoms",&gemmi::Residue::atoms)
    .function("empty_copy",&gemmi::Residue::empty_copy)
    .function("children",select_overload<std::vector<gemmi::Atom>&()>(&gemmi::Residue::children))
    .function("children_const",select_overload<const std::vector<gemmi::Atom>&()const>(&gemmi::Residue::children))
    .function("get",&gemmi::Residue::get)
    .function("sole_atom",&gemmi::Residue::sole_atom)
    .function("same_conformer",&gemmi::Residue::same_conformer)
    .function("is_water",&gemmi::Residue::is_water)
    //.property("sifts_unp",&gemmi::Residue::sifts_unp) //SiftsUnpResidue
    //UniqProxy<Atom> first_conformer() { return {atoms}; }
    //ConstUniqProxy<Atom> first_conformer() const { return {atoms}; }
    //And various pointer return methods ...
    ;

    class_<gemmi::Atom>("GemmiAtom")
    .property("name",&gemmi::Atom::name)
    .property("altloc",&gemmi::Atom::altloc)
    .property("charge",&gemmi::Atom::charge)
    .property("element",&gemmi::Atom::element)
    .property("calc_flag",&gemmi::Atom::calc_flag)
    .property("flag",&gemmi::Atom::flag)
    .property("tls_group_id",&gemmi::Atom::tls_group_id)
    .property("serial",&gemmi::Atom::serial)
    .property("pos",&gemmi::Atom::pos)
    .property("occ",&gemmi::Atom::occ)
    .property("b_iso",&gemmi::Atom::b_iso)
    .function("altloc_or",&gemmi::Atom::altloc_or)
    .function("altloc_matches",&gemmi::Atom::altloc_matches)
    .function("group_key",&gemmi::Atom::group_key)
    .function("has_altloc",&gemmi::Atom::has_altloc)
    .function("b_eq",&gemmi::Atom::b_eq)
    .function("is_hydrogen",&gemmi::Atom::is_hydrogen)
    .function("padded_name",&gemmi::Atom::padded_name)
    .function("empty_copy",&gemmi::Atom::empty_copy)
    .property("aniso",&gemmi::Atom::aniso)//SMat33<float>
    ;

    class_<gemmi::Element>("Element")
    .property("elem",&gemmi::Element::elem)
    .function("ordinal",&gemmi::Element::ordinal)
    .function("atomic_number",&gemmi::Element::atomic_number)
    .function("is_hydrogen",&gemmi::Element::is_hydrogen)
    .function("weight",&gemmi::Element::weight)
    .function("covalent_r",&gemmi::Element::covalent_r)
    .function("vdw_r",&gemmi::Element::vdw_r)
    .function("is_metal",&gemmi::Element::is_metal)
    //.function("name",&gemmi::Element::name, allow_raw_pointers())// These 2 do not work! PKc (char*) is unbound type/
    //.function("uname",&gemmi::Element::uname, allow_raw_pointers())
    ;

    class_<gemmi::Mat33>("Mat33")
    .function("row_copy",&gemmi::Mat33::row_copy)
    .function("column_copy",&gemmi::Mat33::column_copy)
    .function("multiplyVec3",select_overload<gemmi::Vec3(const gemmi::Vec3&)const>(&gemmi::Mat33::multiply))
    .function("multiplyMat33",select_overload<gemmi::Mat33(const gemmi::Mat33&)const>(&gemmi::Mat33::multiply))
    .function("left_multiply",&gemmi::Mat33::left_multiply)
    .function("multiply_by_diagonal",&gemmi::Mat33::multiply_by_diagonal)
    .function("transpose",&gemmi::Mat33::transpose)
    .function("trace",&gemmi::Mat33::trace)
    .function("approx",&gemmi::Mat33::approx)
    .function("determinant",&gemmi::Mat33::determinant)
    .function("inverse",&gemmi::Mat33::inverse)
    .function("is_identity",&gemmi::Mat33::is_identity)
    .function("column_dot",&gemmi::Mat33::column_dot)
    ;

    class_<GemmiSMat33double>("SMat33double")
    .function("as_mat33",&GemmiSMat33double::as_mat33)
    .function("trace",&GemmiSMat33double::trace)
    .function("all_zero",&GemmiSMat33double::all_zero)
    //.function("scale",&GemmiSMat33double::scale) // this causes compiler to give a const usage error.
    //.function("scaled",&GemmiSMat33double::scaled) // this causes all kinds of trouble
    .function("added_kI",&GemmiSMat33double::added_kI)
    .function("r_u_r",select_overload<double(const gemmi::Vec3&)const>(&GemmiSMat33double::r_u_r))
    .function("r_u_rArray",select_overload<double(const std::array<int,3>&)const>(&GemmiSMat33double::r_u_r))
    .function("multiply",&GemmiSMat33double::multiply)
    //.function("transformed_by",&GemmiSMat33double::transformed_by)
    .function("determinant",&GemmiSMat33double::determinant)
    .function("inverse",&GemmiSMat33double::inverse)
    .function("calculate_eigenvalues",&gemmi::SMat33<double>::calculate_eigenvalues) //Hmm, returns std::array<double, 3>
    .function("calculate_eigenvector",&GemmiSMat33double::calculate_eigenvector)
    ;

    class_<GemmiSMat33float>("SMat33float")
    .function("as_mat33",&GemmiSMat33float::as_mat33)
    .function("trace",&GemmiSMat33float::trace)
    .function("all_zero",&GemmiSMat33float::all_zero)
    //.function("scale",&GemmiSMat33float::scale) // this causes compiler to give a const usage error.
    //.function("scaled",&GemmiSMat33float::scaled) // this causes all kinds of trouble
    .function("added_kI",&GemmiSMat33float::added_kI)
    .function("r_u_r",select_overload<double(const gemmi::Vec3&)const>(&GemmiSMat33float::r_u_r))
    .function("r_u_rArray",select_overload<double(const std::array<int,3>&)const>(&GemmiSMat33float::r_u_r))
    .function("multiply",&GemmiSMat33float::multiply)
    //.function("transformed_by",&GemmiSMat33float::transformed_by)
    .function("determinant",&GemmiSMat33float::determinant)
    .function("inverse",&GemmiSMat33float::inverse)
    .function("calculate_eigenvalues",&gemmi::SMat33<float>::calculate_eigenvalues) //Hmm, returns std::array<double, 3>
    .function("calculate_eigenvector",&GemmiSMat33float::calculate_eigenvector)
    ;

    class_<gemmi::Vec3>("GemmiVec3")
    .property("x",&gemmi::Vec3::x)
    .property("y",&gemmi::Vec3::y)
    .property("z",&gemmi::Vec3::z)
    .function("at",select_overload<double(int)const>(&gemmi::Vec3::at))
    .function("negated",&gemmi::Vec3::negated)
    .function("dot",&gemmi::Vec3::dot)
    .function("cross",&gemmi::Vec3::cross)
    .function("length_sq",&gemmi::Vec3::length_sq)
    .function("length",&gemmi::Vec3::length)
    .function("changed_magnitude",&gemmi::Vec3::changed_magnitude)
    .function("normalized",&gemmi::Vec3::normalized)
    .function("dist_sq",&gemmi::Vec3::dist_sq)
    .function("dist",&gemmi::Vec3::dist)
    .function("cos_angle",&gemmi::Vec3::cos_angle)
    .function("angle",&gemmi::Vec3::angle)
    .function("approx",&gemmi::Vec3::approx)
    .function("str",&gemmi::Vec3::str)
    ;

    class_<gemmi::Fractional, base<gemmi::Vec3>>("Fractional")
    .function("wrap_to_unit",&gemmi::Fractional::wrap_to_unit)
    .function("wrap_to_zero",&gemmi::Fractional::wrap_to_zero)
    .function("round",&gemmi::Fractional::round)
    .function("move_toward_zero_by_one",&gemmi::Fractional::move_toward_zero_by_one)
    ;

    class_<gemmi::Position, base<gemmi::Vec3>>("Position")
    ;

    class_<gemmi::Entity::DbRef>("EntityDbRef")
    .property("db_name",&gemmi::Entity::DbRef::db_name)
    .property("accession_code",&gemmi::Entity::DbRef::accession_code)
    .property("id_code",&gemmi::Entity::DbRef::id_code)
    .property("isoform",&gemmi::Entity::DbRef::isoform)
    .property("seq_begin",&gemmi::Entity::DbRef::seq_begin)
    .property("seq_end",&gemmi::Entity::DbRef::seq_end)
    .property("db_begin",&gemmi::Entity::DbRef::db_begin)
    .property("db_end",&gemmi::Entity::DbRef::db_end)
    .property("label_seq_begin",&gemmi::Entity::DbRef::label_seq_begin)
    .property("label_seq_end",&gemmi::Entity::DbRef::label_seq_end)
    ;

    class_<gemmi::Entity>("Entity")
    .property("name",&gemmi::Entity::name)
    .property("subchains",&gemmi::Entity::subchains)
    .property("entity_type",&gemmi::Entity::entity_type)
    .property("polymer_type",&gemmi::Entity::polymer_type)
    .property("sifts_unp_acc",&gemmi::Entity::sifts_unp_acc)
    .property("full_sequence",&gemmi::Entity::full_sequence)
    .property("dbrefs",&gemmi::Entity::dbrefs)
    .function("first_mon",&gemmi::Entity::first_mon)
    ;

    class_<gemmi::ItemGroup<gemmi::Atom>>("ItemGroupAtom")
    .function("size",&gemmi::ItemGroup<gemmi::Atom>::size)
    .function("extent",&gemmi::ItemGroup<gemmi::Atom>::extent)
    .function("empty",&gemmi::ItemGroup<gemmi::Atom>::empty)
    .function("front",select_overload<gemmi::Atom&()>(&gemmi::ItemGroup<gemmi::Atom>::front))
    .function("front_const",select_overload<const gemmi::Atom&()const>(&gemmi::ItemGroup<gemmi::Atom>::front))
    .function("back",select_overload<gemmi::Atom&()>(&gemmi::ItemGroup<gemmi::Atom>::back))
    .function("back_const",select_overload<const gemmi::Atom&()const>(&gemmi::ItemGroup<gemmi::Atom>::back))
    ;
    class_<gemmi::AtomGroup, base<gemmi::ItemGroup<gemmi::Atom>>>("AtomGroup")
    .function("name",&gemmi::AtomGroup::name)
    .function("by_altloc",&gemmi::AtomGroup::by_altloc)
    ;

    class_<gemmi::SpaceGroup>("SpaceGroup")
    .property("number",&gemmi::SpaceGroup::number)
    .property("ccp4",&gemmi::SpaceGroup::ccp4)
    .property("ext",&gemmi::SpaceGroup::ext)
    .property("basisop_idx",&gemmi::SpaceGroup::basisop_idx)
    .function("xhm",&gemmi::SpaceGroup::xhm)
    .function("centring_type",&gemmi::SpaceGroup::centring_type)
    .function("ccp4_lattice_type",&gemmi::SpaceGroup::ccp4_lattice_type)
    .function("short_name",&gemmi::SpaceGroup::short_name)
    .function("pdb_name",&gemmi::SpaceGroup::pdb_name)
    .function("is_sohncke",&gemmi::SpaceGroup::is_sohncke)
    .function("is_enantiomorphic",&gemmi::SpaceGroup::is_enantiomorphic)
    .function("is_symmorphic",&gemmi::SpaceGroup::is_symmorphic)
    .function("is_centrosymmetric",&gemmi::SpaceGroup::is_centrosymmetric)
    .function("point_group",&gemmi::SpaceGroup::point_group)
    .function("laue_class",&gemmi::SpaceGroup::laue_class)
    .function("basisop",&gemmi::SpaceGroup::basisop)
    .function("centred_to_primitive",&gemmi::SpaceGroup::centred_to_primitive)
    .function("operations",&gemmi::SpaceGroup::operations)
    //I do not see a way to wrap these, so I have created utility methods above.
    //I guess adding accessors to class would be only solution?
    //.property("hm",&gemmi::SpaceGroup::hm)
    //.property("qualifier",&gemmi::SpaceGroup::qualifier)
    //.property("hall",&gemmi::SpaceGroup::hall)
    ;

    class_<gemmi::Transform>("Transform")
    .property("mat",&gemmi::Transform::mat)
    .property("vec",&gemmi::Transform::vec)
    .function("inverse",&gemmi::Transform::inverse)
    .function("apply",&gemmi::Transform::apply)
    .function("combine",&gemmi::Transform::combine)
    .function("is_identity",&gemmi::Transform::is_identity)
    .function("set_identity",&gemmi::Transform::set_identity)
    .function("approx",&gemmi::Transform::approx)
    ;

    class_<gemmi::FTransform,base<gemmi::Transform>>("FTransform")
    .function("apply",&gemmi::FTransform::apply)
    ;

    class_<gemmi::Op>("Op")
    .property("rot",&gemmi::Op::rot) //No idea if this can work - Rot
    .property("tran",&gemmi::Op::tran) //No idea if this can work - Tran
    .function("triplet",&gemmi::Op::triplet)
    .function("inverse",&gemmi::Op::inverse)
    .function("wrap",&gemmi::Op::wrap)
    .function("translate",&gemmi::Op::translate)
    .function("translated",&gemmi::Op::translated) //No idea if this can work - Tran
    .function("add_centering",&gemmi::Op::add_centering) //No idea if this can work - Tran
    .function("negated_rot",&gemmi::Op::negated_rot) //No idea if this can work - Rot
    .function("transposed_rot",&gemmi::Op::transposed_rot) //No idea if this can work - Rot
    .function("det_rot",&gemmi::Op::det_rot)
    .function("rot_type",&gemmi::Op::rot_type)
    .function("combine",&gemmi::Op::combine)
    .function("apply_to_xyz",&gemmi::Op::apply_to_xyz)// ? const std::array<double, 3> arg and return
    .function("apply_to_hkl_without_division",&gemmi::Op::apply_to_hkl_without_division) //No idea if this can work - Miller
    .function("divide_hkl_by_DEN",&gemmi::Op::divide_hkl_by_DEN) //No idea if this can work - Miller std::array<int, 3>
    .function("apply_to_hkl",&gemmi::Op::apply_to_hkl) //No idea if this can work - Miller std::array<int, 3>
    .function("phase_shift",&gemmi::Op::phase_shift) //No idea if this can work - Miller std::array<int, 3>
    .function("int_seitz",&gemmi::Op::int_seitz)// ? std::array<std::array<int, 4>, 4> return
    .function("float_seitz",&gemmi::Op::float_seitz)// ? std::array<std::array<int, 4>, 4> return
    //.function("identity",&gemmi::Op::identity) // does not work. Because static?
    ;

    class_<gemmi::GroupOps>("GroupOps")
    .property("sym_ops",&gemmi::GroupOps::sym_ops)
    .function("order",&gemmi::GroupOps::order)
    .function("add_missing_elements",&gemmi::GroupOps::add_missing_elements)
    .function("add_missing_elements_part2",&gemmi::GroupOps::add_missing_elements_part2)
    .function("add_inversion",&gemmi::GroupOps::add_inversion)
    .function("find_centering",&gemmi::GroupOps::find_centering)
    .function("is_centrosymmetric",&gemmi::GroupOps::is_centrosymmetric)
    .function("is_reflection_centric",&gemmi::GroupOps::is_reflection_centric)
    .function("epsilon_factor",&gemmi::GroupOps::epsilon_factor)
    .function("epsilon_factor_without_centering",&gemmi::GroupOps::epsilon_factor_without_centering)
    .function("has_phase_shift",&gemmi::GroupOps::has_phase_shift)
    .function("is_systematically_absent",&gemmi::GroupOps::is_systematically_absent)
    .function("change_basis_impl",&gemmi::GroupOps::change_basis_impl)
    .function("change_basis_forward",&gemmi::GroupOps::change_basis_forward)
    .function("change_basis_backward",&gemmi::GroupOps::change_basis_backward)
    .function("all_ops_sorted",&gemmi::GroupOps::all_ops_sorted)
    .function("get_op",&gemmi::GroupOps::get_op)
    .function("is_same_as",&gemmi::GroupOps::is_same_as)
    .function("has_same_centring",&gemmi::GroupOps::has_same_centring)
    .function("has_same_rotations",&gemmi::GroupOps::has_same_rotations)
    .function("find_grid_factors",&gemmi::GroupOps::find_grid_factors)// return std::array<int, 3>
    .function("are_directions_symmetry_related",&gemmi::GroupOps::are_directions_symmetry_related)
    .function("derive_symmorphic",&gemmi::GroupOps::derive_symmorphic)
    //.property("sym_ops",&gemmi::GroupOps::sym_ops) // Op::Tran
    ;

    class_<gemmi::Helix>("Helix")
    .property("start",&gemmi::Helix::start)
    .property("end",&gemmi::Helix::end)
    .property("pdb_helix_class",&gemmi::Helix::pdb_helix_class)
    .property("length",&gemmi::Helix::length)
    .function("set_helix_class_as_int",&gemmi::Helix::set_helix_class_as_int)
    ;
    class_<gemmi::Sheet::Strand>("Strand")
    .property("start",&gemmi::Sheet::Strand::start)
    .property("end",&gemmi::Sheet::Strand::end)
    .property("hbond_atom2",&gemmi::Sheet::Strand::hbond_atom2)
    .property("hbond_atom1",&gemmi::Sheet::Strand::hbond_atom1)
    .property("sense",&gemmi::Sheet::Strand::sense)
    .property("name",&gemmi::Sheet::Strand::name)
    ;
    class_<gemmi::Sheet>("Sheet")
    .property("name",&gemmi::Sheet::name)
    .property("strands",&gemmi::Sheet::strands)
    ;
    class_<gemmi::Connection>("Connection")
    .property("name",&gemmi::Connection::name)
    .property("link_id",&gemmi::Connection::link_id)
    .property("type",&gemmi::Connection::type)
    .property("asu",&gemmi::Connection::asu)
    .property("partner1",&gemmi::Connection::partner1)
    .property("partner2",&gemmi::Connection::partner2)
    .property("reported_distance",&gemmi::Connection::reported_distance)
    ;
    class_<gemmi::AtomAddress>("AtomAddress")
    .property("chain_name",&gemmi::AtomAddress::chain_name)
    .property("res_id",&gemmi::AtomAddress::res_id)
    .property("atom_name",&gemmi::AtomAddress::atom_name)
    .property("altloc",&gemmi::AtomAddress::altloc)
    .function("str",&gemmi::AtomAddress::str)
    ;

    class_<gemmi::Assembly::Operator>("AssemblyOperator")
    .property("name",&gemmi::Assembly::Operator::name)
    .property("type",&gemmi::Assembly::Operator::type)
    .property("transform",&gemmi::Assembly::Operator::transform)
    ;

    class_<gemmi::Assembly::Gen>("AssemblyGen")
    .property("chains",&gemmi::Assembly::Gen::chains)
    .property("subchains",&gemmi::Assembly::Gen::subchains)
    .property("operators",&gemmi::Assembly::Gen::operators)
    ;

    class_<gemmi::Assembly>("Assembly")
    .property("name",&gemmi::Assembly::name)
    .property("author_determined",&gemmi::Assembly::author_determined)
    .property("software_determined",&gemmi::Assembly::software_determined)
    .property("special_kind",&gemmi::Assembly::special_kind)
    .property("oligomeric_count",&gemmi::Assembly::oligomeric_count)
    .property("oligomeric_details",&gemmi::Assembly::oligomeric_details)
    .property("software_name",&gemmi::Assembly::software_name)
    .property("absa",&gemmi::Assembly::absa)
    .property("ssa",&gemmi::Assembly::ssa)
    .property("more",&gemmi::Assembly::more)
    .property("generators",&gemmi::Assembly::generators)
    ;

    class_<gemmi::NcsOp>("NcsOp")
    .property("id",&gemmi::NcsOp::id)
    .property("given",&gemmi::NcsOp::given)
    .property("tr",&gemmi::NcsOp::tr)
    .function("apply",&gemmi::NcsOp::apply)
    ;

    class_<gemmi::Metadata>("Metadata")
    .property("authors",&gemmi::Metadata::authors)
    .property("experiments",&gemmi::Metadata::experiments)
    .property("crystals",&gemmi::Metadata::crystals)
    .property("refinement",&gemmi::Metadata::refinement)
    .property("software",&gemmi::Metadata::software)
    .property("solved_by",&gemmi::Metadata::solved_by)
    .property("starting_model",&gemmi::Metadata::starting_model)
    .property("remark_300_detail",&gemmi::Metadata::remark_300_detail)
    .function("has_restr",&gemmi::Metadata::has_restr)
    .function("has_tls",&gemmi::Metadata::has_tls)
    .function("has_d",select_overload<bool(double gemmi::RefinementInfo::*field)const>(&gemmi::Metadata::has))
    .function("has_i",select_overload<bool(int gemmi::RefinementInfo::*field)const>(&gemmi::Metadata::has))
    .function("has_s",select_overload<bool(std::string gemmi::RefinementInfo::*field)const>(&gemmi::Metadata::has))
    .function("has_m33",select_overload<bool(gemmi::Mat33 gemmi::RefinementInfo::*field)const>(&gemmi::Metadata::has))
    ;

    class_<gemmi::SoftwareItem>("SoftwareItem")
    .property("name",&gemmi::SoftwareItem::name)
    .property("version",&gemmi::SoftwareItem::version)
    .property("date",&gemmi::SoftwareItem::date)
    .property("classification",&gemmi::SoftwareItem::classification)
    .property("pdbx_ordinal",&gemmi::SoftwareItem::pdbx_ordinal)
    ;

    class_<gemmi::ExperimentInfo>("ExperimentInfo")
    .property("method",&gemmi::ExperimentInfo::method)
    .property("number_of_crystals",&gemmi::ExperimentInfo::number_of_crystals)
    .property("unique_reflections",&gemmi::ExperimentInfo::unique_reflections)
    .property("reflections",&gemmi::ExperimentInfo::reflections)
    .property("b_wilson",&gemmi::ExperimentInfo::b_wilson)
    .property("shells",&gemmi::ExperimentInfo::shells)
    .property("diffraction_ids",&gemmi::ExperimentInfo::diffraction_ids)
    ;

    class_<gemmi::DiffractionInfo>("DiffractionInfo")
    .property("id",&gemmi::DiffractionInfo::id)
    .property("temperature",&gemmi::DiffractionInfo::temperature)
    .property("source",&gemmi::DiffractionInfo::source)
    .property("source_type",&gemmi::DiffractionInfo::source_type)
    .property("synchrotron",&gemmi::DiffractionInfo::synchrotron)
    .property("beamline",&gemmi::DiffractionInfo::beamline)
    .property("wavelengths",&gemmi::DiffractionInfo::wavelengths)
    .property("scattering_type",&gemmi::DiffractionInfo::scattering_type)
    .property("mono_or_laue",&gemmi::DiffractionInfo::mono_or_laue)
    .property("monochromator",&gemmi::DiffractionInfo::monochromator)
    .property("collection_date",&gemmi::DiffractionInfo::collection_date)
    .property("optics",&gemmi::DiffractionInfo::optics)
    .property("detector",&gemmi::DiffractionInfo::detector)
    .property("detector_make",&gemmi::DiffractionInfo::detector_make)
    ;

    class_<gemmi::ReflectionsInfo>("ReflectionsInfo")
    .property("resolution_high",&gemmi::ReflectionsInfo::resolution_high)
    .property("resolution_low",&gemmi::ReflectionsInfo::resolution_low)
    .property("completeness",&gemmi::ReflectionsInfo::completeness)
    .property("redundancy",&gemmi::ReflectionsInfo::redundancy)
    .property("r_merge",&gemmi::ReflectionsInfo::r_merge)
    .property("r_sym",&gemmi::ReflectionsInfo::r_sym)
    .property("mean_I_over_sigma",&gemmi::ReflectionsInfo::mean_I_over_sigma)
    ;

    class_<gemmi::CrystalInfo>("CrystalInfo")
    .property("id",&gemmi::CrystalInfo::id)
    .property("description",&gemmi::CrystalInfo::description)
    .property("ph",&gemmi::CrystalInfo::ph)
    .property("ph_range",&gemmi::CrystalInfo::ph_range)
    .property("diffractions",&gemmi::CrystalInfo::diffractions)
    ;

    class_<gemmi::BasicRefinementInfo>("BasicRefinementInfo")
    .property("resolution_high",&gemmi::BasicRefinementInfo::resolution_high)
    .property("resolution_low",&gemmi::BasicRefinementInfo::resolution_low)
    .property("completeness",&gemmi::BasicRefinementInfo::completeness)
    .property("reflection_count",&gemmi::BasicRefinementInfo::reflection_count)
    .property("rfree_set_count",&gemmi::BasicRefinementInfo::rfree_set_count)
    .property("r_all",&gemmi::BasicRefinementInfo::r_all)
    .property("r_work",&gemmi::BasicRefinementInfo::r_work)
    .property("r_free",&gemmi::BasicRefinementInfo::r_free)
    ;

    class_<gemmi::TlsGroup::Selection>("TlsGroupSelection")
    .property("chain",&gemmi::TlsGroup::Selection::chain)
    .property("res_begin",&gemmi::TlsGroup::Selection::res_begin)
    .property("res_end",&gemmi::TlsGroup::Selection::res_end)
    .property("details",&gemmi::TlsGroup::Selection::details)
    ;

    class_<gemmi::TlsGroup>("TlsGroup")
    .property("id",&gemmi::TlsGroup::id)
    .property("selections",&gemmi::TlsGroup::selections)
    .property("origin",&gemmi::TlsGroup::origin)
    .property("T",&gemmi::TlsGroup::T)
    .property("L",&gemmi::TlsGroup::L)
    .property("S",&gemmi::TlsGroup::S)
    ;

    class_<gemmi::RefinementInfo::Restr>("Restr")
    .property("name",&gemmi::RefinementInfo::Restr::name)
    .property("count",&gemmi::RefinementInfo::Restr::count)
    .property("weight",&gemmi::RefinementInfo::Restr::weight)
    .property("function",&gemmi::RefinementInfo::Restr::function)
    .property("dev_ideal",&gemmi::RefinementInfo::Restr::dev_ideal)
    ;

    class_<gemmi::RefinementInfo,base<gemmi::BasicRefinementInfo>>("RefinementInfo")
    .property("id",&gemmi::RefinementInfo::id)
    .property("cross_validation_method",&gemmi::RefinementInfo::cross_validation_method)
    .property("rfree_selection_method",&gemmi::RefinementInfo::rfree_selection_method)
    .property("bin_count",&gemmi::RefinementInfo::bin_count)
    .property("bins",&gemmi::RefinementInfo::bins)
    .property("mean_b",&gemmi::RefinementInfo::mean_b)
    .property("aniso_b",&gemmi::RefinementInfo::aniso_b)
    .property("luzzati_error",&gemmi::RefinementInfo::luzzati_error)
    .property("dpi_blow_r",&gemmi::RefinementInfo::dpi_blow_r)
    .property("dpi_blow_rfree",&gemmi::RefinementInfo::dpi_blow_rfree)
    .property("dpi_cruickshank_r",&gemmi::RefinementInfo::dpi_cruickshank_r)
    .property("dpi_cruickshank_rfree",&gemmi::RefinementInfo::dpi_cruickshank_rfree)
    .property("cc_fo_fc",&gemmi::RefinementInfo::cc_fo_fc)
    .property("cc_fo_fc_free",&gemmi::RefinementInfo::cc_fo_fc_free)
    .property("restr_stats",&gemmi::RefinementInfo::restr_stats)
    .property("tls_groups",&gemmi::RefinementInfo::tls_groups)
    .property("remarks",&gemmi::RefinementInfo::remarks)
    ;

    class_<gemmi::Structure>("Structure")
    .constructor<>()
    .property("name",&gemmi::Structure::name)
    .property("spacegroup_hm",&gemmi::Structure::spacegroup_hm)
    .property("has_origx",&gemmi::Structure::has_origx)
    .property("models",&gemmi::Structure::models)
    .property("ncs",&gemmi::Structure::ncs)
    .property("entities",&gemmi::Structure::entities)
    .property("connections",&gemmi::Structure::connections)
    .property("helices",&gemmi::Structure::helices)
    .property("sheets",&gemmi::Structure::sheets)
    .property("assemblies",&gemmi::Structure::assemblies)
    .property("cell",&gemmi::Structure::cell)
    .property("meta",&gemmi::Structure::meta)
    .property("origx",&gemmi::Structure::origx)
    .property("resolution",&gemmi::Structure::resolution)
    .property("raw_remarks",&gemmi::Structure::raw_remarks)
    .property("input_format",&gemmi::Structure::input_format)
    .function("get_info",&gemmi::Structure::get_info)
    .function("remove_model",&gemmi::Structure::remove_model)
    .function("renumber_models",&gemmi::Structure::renumber_models)
    .function("ncs_given_count",&gemmi::Structure::ncs_given_count)
    .function("get_ncs_multiplier",&gemmi::Structure::get_ncs_multiplier)
    .function("ncs_not_expanded",&gemmi::Structure::ncs_not_expanded)
    .function("merge_chain_parts",&gemmi::Structure::merge_chain_parts)
    .function("remove_empty_chains",&gemmi::Structure::remove_empty_chains)
    .function("empty_copy",&gemmi::Structure::empty_copy)
    .function("setup_cell_images",&gemmi::Structure::setup_cell_images)
    .function("first_model",select_overload<const gemmi::Model&(void)const>(&gemmi::Structure::first_model))
    ;

    //TODO Wrap the following
    class_<gemmi::CraProxy>("CraProxy")
    ;
    class_<gemmi::ConstCraProxy>("ConstCraProxy")
    ;
    class_<gemmi::const_CRA>("const_CRA")
    ;
    class_<gemmi::NearestImage>("NearestImage")
    ;
    class_<gemmi::Miller>("MillerHash")
    ;

    // And possibly all/some of these
    /*
Binner
HklMatch
CenterOfMass
IT92
C4322
NeighborSearch
TwoFoldData
AsuBrick
SolventMasker
NodeInfo
FloodFill
ChemLink
ChemMod
EnerLib
MonLib
BondIndex
Topo
ComplexCorrelation
HklValue
ValueSigma
AsuData
DensityCalculator
AtomNameElement
GridOp
GridMeta
GridBase
Grid
ContactSearch
ReciprocalGrid
LinkHunt
PdbReadOptions
ResidueSpan::GroupingProxy
SiftsUnpResidue
MmcifOutputGroups
Blob
BlobCriteria
GridConstPoint
Box
Variance
Covariance
Correlation
DataStats
UnmergedHklMover
Mtz
MtzDataProxy
MtzExternalDataProxy
Intensities
from_chars_result
parse_options
span
value128
adjusted_mantissa
parsed_number_string
powers_template
stackvec
bigint
parsed_number_string
util
ExecC2C
ExecHartley
ExecDcst
ExecR2R
XdsAscii
Restraints
ChemComp
Scaling
Gaus
Point
OrbitalCoef
SellingVector;
GruberVector
SellingVector
ExpSum
ExpAnisoSum
GaussianCoef
AlignmentScoring
AlignmentResult
Addends
SpaceGroupAltName
Tables_
ReciprocalAsu
Neutron92
PdbWriteOptions
LoopArg
FrameArg
CommentArg
Loop
Table
Document
ChainNameGenerator
AssemblyMapping
FileStream
MemoryStream
ReflnBlock
ReflnDataProxy
Selection
ResidueInfo
FPhiProxy
CifToMtz
Element
Ccp4Base
Ccp4
BidirIterator
UniqProxy
ConstUniqProxy
FilterProxy
ConstFilterProxy
SmallStructure
SupResult
Ofstream
Ifstream
LevMar
BesselTables_
IsMmCifFile
IsCifFile
IsPdbFile
IsCoordinateFile
IsAnyFile
IsMatchingFile
GlobWalk
    */

    //TODO Here we need to put *lots* of gemmi functions
    function("read_structure_file",&gemmi::read_structure_file);
    function("get_spacegroup_by_name",&gemmi::get_spacegroup_by_name);

    //Utilities to deal with char*/[]
    function("getSpaceGroupHMAsString",&get_spacegroup_hm);
    function("getSpaceGroupHallAsString",&get_spacegroup_hall);
    function("getSpaceGroupQualifierAsString",&get_spacegroup_qualifier);
    function("getElementNameAsString",&get_element_name_as_string);
}
