// This file is part of TetWild, a software for generating tetrahedral meshes.
//
// Copyright (C) 2018 Yixin Hu <yixin.hu@nyu.edu>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
//
// Created by Yixin Hu on 5/31/18.
//
#ifndef TETWILD_TETWILD_H
#define TETWILD_TETWILD_H

#include <array>
#include <vector>
#include <string>
#include <Eigen/Dense>

namespace tetwild {

// Global arguments controlling the behavior of TetWild
struct Args {
    // Target edge-length = bbox diagonal / initial_edge_len_rel
    double initial_edge_len_rel = 20;

    // Target epsilon = bbox_diagonal / eps_rel
    double eps_rel = 1000;

    //////////////////////
    // Advanced options //
    //////////////////////

    // Explicitly specify a sampling distance for triangles (= bbox_diagonal / sampling_dist)
    int sampling_dist_rel = -1;

    // Run the algorithm in stage (as explain in p.8 of the paper)
    // If the first stage didn't succeed, call again with `stage = 2`,  etc.
    int stage = 1;

    // Multiplier for resizing the target-edge length around bad-quality vertices
    // See MeshRefinement::updateScalarField() for more details
    double adaptive_scalar = 0.6;

    // Energy threshold
    // If the max tet energy is below this threshold, the mesh optimization process is stopped.
    // Also used to determine where to resize the scalar field (if a tet incident to a vertex has larger energy than this threshold, then resize around this vertex).
    double filter_energy_thres = 10;

    // Threshold on the energy delta (avg and max) below which to rescale the target edge length scalar field
    double delta_energy_thres = 0.1;

    // Maximum number of mesh optimization iterations
    int max_num_passes = 80;

    // Sample points at voxel centers for initial Delaunay triangulation
    bool use_voxel_stuffing = true;

    // Use Laplacian smoothing on the faces/vertices covering an open boundary after the mesh optimization step (post-processing)
    bool smooth_open_boundary = false;

    // Target number of vertices (minimum), within 5% of tolerance
    int target_num_vertices = -1;

    // Background mesh for the edge length sizing field
    std::string background_mesh = "";

    // [debug] logging
    int write_csv_file = true;
    std::string postfix = "_";
    std::string csv_file = "";
    std::string slz_file = "";
    int save_mid_result = -1; // save intermediate result

    bool is_quiet = false;

    static Args & args() {
        static Args ag;
        return ag;
    }

private:
    Args() = default;
};


///
/// Robust tetrahedralization of an input triangle soup, with an envelop constraint.
///
/// @param[in]  VI    { #VI x 3 input mesh vertices }
/// @param[in]  FI    { #FI x 3 input mesh triangles }
/// @param[out] VO    { #VO x 3 output mesh vertices }
/// @param[out] TO    { #TO x 4 output mesh tetrahedra }
/// @param[out] A     { #TO x 1 array of min dihedral angle over each tet }
///
void tetrahedralization(const Eigen::MatrixXd &VI, const Eigen::MatrixXi &FI,
    Eigen::MatrixXd &VO, Eigen::MatrixXi &TO, Eigen::VectorXd &AO);

///
/// Extract the boundary facets of a triangle mesh, removing unreferenced vertices
///
/// @param[in]  VI    { #VI x 3 input mesh vertices }
/// @param[in]  TI    { #TI x 4 input mesh tetrahedra }
/// @param[out] VS    { #VS x 3 output mesh vertices }
/// @param[out] FS    { #FS x 3 output mesh triangles }
///
void extractSurfaceMesh(const Eigen::MatrixXd &VI, const Eigen::MatrixXi &TI,
    Eigen::MatrixXd &VS, Eigen::MatrixXi &FS);

}

#endif //TETWILD_TETWILD_H
