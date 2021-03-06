// This file is part of TetWild, a software for generating tetrahedral meshes.
//
// Copyright (C) 2018 Jeremie Dumas <jeremie.dumas@ens-lyon.org>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
//
// Created by Jeremie Dumas on 09/04/18.
//
////////////////////////////////////////////////////////////////////////////////

#include <tetwild/Logger.h>
#include <tetwild/mmg/Remeshing.h>
#include <tetwild/geogram/Utils.h>
#include <geogram/basic/common.h>
#include <geogram/basic/command_line.h>
#include <geogram/basic/command_line_args.h>
#include <geogram/mesh/mesh_io.h>
#include <igl/bounding_box_diagonal.h>
#include <igl/read_triangle_mesh.h>
#include <igl/write_triangle_mesh.h>
#include <igl/remove_duplicate_vertices.h>
#include <igl/writeMESH.h>
#include <tetwild/DisableWarnings.h>
#include <CLI/CLI.hpp>
#include <tetwild/EnableWarnings.h>

int main(int argc, char *argv[]) {
    struct {
        std::string input;
        std::string output = "output.mesh";
        double mesh_size = 0.0;
        double epsilon = 0.0;
        int num_samples = 0;
        bool sharp = false;
        int log_level = 1; // debug
    } args;

    CLI::App app{"MMG_Wrapper"};
    app.add_option("input,--input", args.input, "Input mesh")->required()->check(CLI::ExistingFile);
    app.add_option("output,--output", args.output, "Output mesh");
    app.add_option("-m,--mesh_size", args.mesh_size, "Maximum mesh size (default: 100% of the bbox diagonal)");
    app.add_option("-e,--epsilon", args.epsilon, "Absolute Hausdorff distance (default: 0.1% of the bbox diagonal)");
    app.add_option("-n,--num_samples", args.num_samples, "Number of samples for the SDF field (default: 1x number of vertices)");
    app.add_option("-l,--level", args.log_level, "Log level (default: debug)");
    app.add_flag("-s,--sharp_features", args.sharp, "Detect sharp features (default: false)");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        return app.exit(e);
    }

    spdlog::set_level(static_cast<spdlog::level::level_enum>(args.log_level));
    spdlog::flush_every(std::chrono::seconds(3));
    GEO::initialize();

    // Import standard command line arguments, and custom ones
    GEO::CmdLine::import_arg_group("standard");
    GEO::CmdLine::import_arg_group("pre");
    GEO::CmdLine::import_arg_group("algo");

    // Load input
    Eigen::MatrixXd VI, SV, VO;
    Eigen::MatrixXi FI, SF, FO, TO;
    igl::read_triangle_mesh(args.input, VI, FI);
    Eigen::VectorXi SVI, SVJ;
    igl::remove_duplicate_vertices(VI, FI, 1e-7 * igl::bounding_box_diagonal(VI), SV, SVI, SVJ, SF);
    VI = SV;
    FI = SF;

    // Compute default arguments
    if (args.mesh_size == 0.0) {
        args.mesh_size = 1.0 * igl::bounding_box_diagonal(VI);
    } else {
        args.mesh_size = args.mesh_size / 100.0 * igl::bounding_box_diagonal(VI);
    }
    if (args.epsilon == 0.0) {
        args.epsilon =  0.1 * igl::bounding_box_diagonal(VI);
    } else {
        args.epsilon = args.epsilon / 100.0 * igl::bounding_box_diagonal(VI);
    }
    if (args.num_samples == 0) {
        args.num_samples = VI.rows();
    }

    // Remesh
    tetwild::MmgOptions opt;
    opt.hmin = std::min(0.1 * args.mesh_size, 0.01 * igl::bounding_box_diagonal(VI));
    opt.hmax = args.mesh_size;
    opt.hausd = args.epsilon;
    opt.angle_detection = args.sharp;
    tetwild::isosurface_remeshing(VI, FI, args.num_samples, VO, FO, TO, opt);

    // Save output
    GEO::Mesh M;
    tetwild::to_geogram_mesh(VO, FO, TO, M);
    mesh_save(M, args.output);

    spdlog::shutdown();

    return 0;
}
