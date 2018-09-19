// This file is part of TetWild, a software for generating tetrahedral meshes.
//
// Copyright (C) 2018 Yixin Hu <yixin.hu@nyu.edu>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#include <tetwild/tetwild.h>
#include <tetwild/Common.h>
#include <tetwild/MeshRefinement.h>
#include <igl/read_triangle_mesh.h>
#include <igl/write_triangle_mesh.h>
#include <pymesh/MshSaver.h>
#include <tetwild/DisableWarnings.h>
#include <CLI/CLI.hpp>
#include <tetwild/EnableWarnings.h>

using namespace tetwild;

namespace tetwild {
    void extractFinalTetmesh(MeshRefinement& MR, Eigen::MatrixXd &V_out, Eigen::MatrixXi &T_out, Eigen::VectorXd &A_out);
} // namespace tetwild

void saveFinalTetmesh(const Eigen::MatrixXd &V, const Eigen::MatrixXi &T, const Eigen::VectorXd &A) {
    std::string output_format = State::state().output_file.substr(State::state().output_file.size() - 4, 4);
    if (output_format == "mesh") {
        std::fstream f(State::state().output_file, std::ios::out);
        f.precision(std::numeric_limits<double>::digits10 + 1);
        f << "MeshVersionFormatted 1" << std::endl;
        f << "Dimension 3" << std::endl;

        f << "Vertices" << std::endl << V.rows() << std::endl;
        for (int i = 0; i < V.rows(); i++)
            f << V(i, 0) << " " << V(i, 1) << " " << V(i, 2) << " " << 0 << std::endl;
        f << "Triangles" << std::endl << 0 <<std::endl;
        f << "Tetrahedra" << std::endl;
        f << T.rows() << std::endl;
        for (int i = 0; i < T.rows(); i++) {
            for (int j = 0; j < 4; j++) {
                f << T(i, j) + 1 << " ";
            }
            f << 0 << std::endl;
        }

        f << "End";
        f.close();
    } else {
        PyMesh::MshSaver mSaver(State::state().output_file, true);
        PyMesh::VectorF V_flat(V.size());
        PyMesh::VectorI T_flat(T.size());
        Eigen::MatrixXd VV = V.transpose();
        Eigen::MatrixXi TT = T.transpose();
        std::copy_n(VV.data(), V.size(), V_flat.data());
        std::copy_n(TT.data(), T.size(), T_flat.data());
        mSaver.save_mesh(V_flat, T_flat, 3, mSaver.TET);
        mSaver.save_elem_scalar_field("min_dihedral_angle", A);
    }

    if (GArgs::args().is_quiet) {
        return;
    }
    Eigen::MatrixXd V_sf;
    Eigen::MatrixXi F_sf;
    extractSurfaceMesh(V, T, V_sf, F_sf);
    igl::writeOBJ(State::state().working_dir+State::state().postfix+"_sf.obj", V_sf, F_sf);
}

void gtet_new_slz(const std::string& sf_file, const std::string& slz_file, int max_pass,
                  const std::array<bool, 4>& ops, Eigen::MatrixXd &VO, Eigen::MatrixXi &TO,
                  Eigen::VectorXd &AO)
{
    MeshRefinement MR;
    MR.deserialization(sf_file, slz_file);

//    MR.is_dealing_unrounded = true;
    MR.refine(State::state().ENERGY_AMIPS, ops, false, true);

    extractFinalTetmesh(MR, VO, TO, AO); //do winding number and output the tetmesh
}

int main(int argc, char *argv[]) {
#ifdef MUTE_COUT
    logger().debug("Unnecessary checks are muted.");
#endif
    int log_level = 1; // debug
    std::string log_filename = "";

    CLI::App app{"RobustTetMeshing"};
    app.add_option("input,--input", GArgs::args().input, "Input surface mesh INPUT in .off/.obj/.stl/.ply format. (string, required)")->required();
    app.add_option("output,--output", GArgs::args().output, "Output tetmesh OUTPUT in .msh format. (string, optional, default: input_file+postfix+'.msh')");
    app.add_option("--postfix", GArgs::args().postfix, "Postfix P for output files. (string, optional, default: '_')");
    app.add_option("-l,--ideal-edge-length", GArgs::args().initial_edge_len_rel, "ideal_edge_length = diag_of_bbox / L. (double, optional, default: 20)");
    app.add_option("-e,--epsilon", GArgs::args().eps_rel, "epsilon = diag_of_bbox / EPS. (double, optional, default: 1000)");
    app.add_option("--stage", GArgs::args().stage, "Run pipeline in stage STAGE. (integer, optional, default: 1)");
    app.add_option("--filter-energy", GArgs::args().filter_energy_thres, "Stop mesh improvement when the maximum energy is smaller than ENERGY. (double, optional, default: 10)");
    app.add_option("--max-pass", GArgs::args().max_num_passes, "Do PASS mesh improvement passes in maximum. (integer, optional, default: 80)");

    app.add_flag("--is-laplacian", GArgs::args().smooth_open_boundary, "Do Laplacian smoothing for the surface of output on the holes of input (optional)");
    app.add_option("--targeted-num-v", GArgs::args().target_num_vertices, "Output tetmesh that contains TV vertices. (integer, optional, tolerance: 5%)");
    app.add_option("--bg-mesh", GArgs::args().background_mesh, "Background tetmesh BGMESH in .msh format for applying sizing field. (string, optional)");
    app.add_flag("-q,--is-quiet", GArgs::args().is_quiet, "Mute console output. (optional)");
    app.add_option("--log", log_filename, "Log info to given file.");
    app.add_option("--level", log_level, "Log level (0 = most verbose, 6 = off).");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        return app.exit(e);
    }

    Logger::init(!GArgs::args().is_quiet, log_filename);
    log_level = std::max(0, std::min(6, log_level));
    spdlog::set_level(static_cast<spdlog::level::level_enum>(log_level));
    spdlog::flush_every(std::chrono::seconds(3));

    // logger().info("this is a test");
    // logger().debug("debug stuff");

    //initalization
    GEO::initialize();
    State::state().postfix = GArgs::args().postfix;
    if(GArgs::args().slz_file != "")
        State::state().working_dir = GArgs::args().input.substr(0, GArgs::args().slz_file.size() - 4);
    else
        State::state().working_dir = GArgs::args().input.substr(0, GArgs::args().input.size() - 4);

    if(GArgs::args().csv_file == "")
        State::state().stat_file = State::state().working_dir + State::state().postfix + ".csv";
    else
        State::state().stat_file = GArgs::args().csv_file;

    if(GArgs::args().output == "")
        State::state().output_file = State::state().working_dir + State::state().postfix + ".msh";
    else
        State::state().output_file = GArgs::args().output;

    if(GArgs::args().is_quiet) {
        GArgs::args().write_csv_file = false;
    }

    //do tetrahedralization
    Eigen::MatrixXd VO;
    Eigen::MatrixXi TO;
    Eigen::VectorXd AO;
    if(GArgs::args().slz_file != "") {
        gtet_new_slz(GArgs::args().input, GArgs::args().slz_file, GArgs::args().max_num_passes,
            {{true, false, true, true}}, VO, TO, AO);
    } else {
        Eigen::MatrixXd VI;
        Eigen::MatrixXi FI;
        igl::read_triangle_mesh(GArgs::args().input, VI, FI);
        tetwild::tetrahedralization(VI, FI, VO, TO, AO);
    }
    saveFinalTetmesh(VO, TO, AO);

    spdlog::shutdown();

    return 0;
}
