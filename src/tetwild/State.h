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

#pragma once

#include <string>

namespace tetwild {

// Global values computed from the user input
struct State {
    const int EPSILON_INFINITE=-2;
    const int EPSILON_NA=-1;
    const int ENERGY_NA=0;
    const int ENERGY_AD=1;
    const int ENERGY_AMIPS=2;
    const int ENERGY_DIRICHLET=3;
    const double MAX_ENERGY = 1e50;
    int NOT_SURFACE = 0;

    // paths used for i/o
    std::string working_dir;
    std::string stat_file;
    std::string postfix;

    double eps = 0; // effective epsilon at the current stage (see \hat{\epsilon} in the paper)
    double eps_2 = 0;
    double sampling_dist = 0; // sampling distance for triangles at the current stage (see d_k p.8 of the paper)
    double initial_edge_len = 0; // initial target edge-length defined by the user (the final lengths can be lower, depending on mesh quality and feature size)
    double bbox_diag = 0; // bbox diagonal
    bool is_mesh_closed = 0; // open mesh or closed mesh?

    double eps_input = 0; // target epsilon entered by the user
    double eps_delta = 0; // increment for the envelope at each sub-stage of the mesh optimization (see (3) p.8 of the paper)
    int sub_stage = 1; // sub-stage within the stage that tetwild was called with

    ///////////////
    // [testing] //
    ///////////////

    // Whether to use the max or the total energy when checking improvements in local operations
    bool use_energy_max = true;

    // Use sampling to determine whether a face lies outside the envelope during mesh optimization
    // (if false, then only its vertices are tested)
    bool use_sampling = true;

    // Project vertices to the plane of their one-ring instead of the original surface during vertex smoothing
    bool use_onering_projection = false;

    // [debug]
    bool is_print_tmp = false;

    static State & state() {
        static State st;
        return st;
    }

private:
    State() = default;
};


struct MeshRecord {
    enum OpType {
        OP_INIT = 0,
        OP_PREPROCESSING,
        OP_DELAUNEY_TETRA,
        OP_DIVFACE_MATCH,
        OP_BSP,
        OP_SIMPLE_TETRA,

        OP_OPT_INIT,
        OP_SPLIT,
        OP_COLLAPSE,
        OP_SWAP,
        OP_SMOOTH,
        OP_ADAP_UPDATE,
        OP_WN,
        OP_UNROUNDED
    };

    int op;
    double timing;
    int n_v;
    int n_t;
    double min_min_d_angle = -1;
    double avg_min_d_angle = -1;
    double max_max_d_angle = -1;
    double avg_max_d_angle = -1;
    double max_energy = -1;
    double avg_energy = -1;

    MeshRecord(int op_, double timing_, int n_v_, int n_t_, double min_min_d_angle_, double avg_min_d_angle_,
               double max_max_d_angle_, double avg_max_d_angle_, double max_energy_, double avg_energy_) {
        this->op = op_;
        this->timing = timing_;
        this->n_v = n_v_;
        this->n_t = n_t_;
        this->min_min_d_angle = min_min_d_angle_;
        this->avg_min_d_angle = avg_min_d_angle_;
        this->max_max_d_angle = max_max_d_angle_;
        this->avg_max_d_angle = avg_max_d_angle_;
        this->max_energy = max_energy_;
        this->avg_energy = avg_energy_;
    }

    MeshRecord(int op_, double timing_, int n_v_, int n_t_) {
        this->op = op_;
        this->timing = timing_;
        this->n_v = n_v_;
        this->n_t = n_t_;
    }
};

} // namespace tetwild
