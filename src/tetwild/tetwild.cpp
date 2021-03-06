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

#include <tetwild/tetwild.h>
#include <tetwild/Common.h>
#include <tetwild/Logger.h>
#include <tetwild/Preprocess.h>
#include <tetwild/DelaunayTetrahedralization.h>
#include <tetwild/BSPSubdivision.h>
#include <tetwild/SimpleTetrahedralization.h>
#include <tetwild/MeshRefinement.h>
#include <tetwild/InoutFiltering.h>
#include <tetwild/Utils.h>
#include <tetwild/Quality.h>
#include <tetwild/mmg/Remeshing.h>
#include <igl/boundary_facets.h>
#include <igl/bounding_box_diagonal.h>
#include <igl/remove_unreferenced.h>
#include <igl/write_triangle_mesh.h>
#include <igl/writeMESH.h>
#include <igl/barycenter.h>
#include <igl/winding_number.h>
#include <pymesh/MshSaver.h>
#include <geogram/mesh/mesh.h>


namespace tetwild {

////////////////////////////////////////////////////////////////////////////////

void printFinalQuality(double time, const std::vector<TetVertex>& tet_vertices,
                       const std::vector<std::array<int, 4>>& tets,
                       const std::vector<bool> &t_is_removed,
                       const std::vector<TetQuality>& tet_qualities,
                       const std::vector<int>& v_ids,
                       const Args &args, const State &state)
{
    logger().debug("final quality:");
    double min = 10, max = 0;
    double min_avg = 0, max_avg = 0;
    // double max_asp_ratio = 0, avg_asp_ratio = 0;
    double max_slim_energy = 0, avg_slim_energy = 0;
    std::array<double, 6> cmp_cnt = {{0, 0, 0, 0, 0, 0}};
    std::array<double, 6> cmp_d_angles = {{6 / 180.0 * M_PI, 12 / 180.0 * M_PI, 18 / 180.0 * M_PI,
                                           162 / 180.0 * M_PI, 168 / 180.0 * M_PI, 174 / 180.0 * M_PI}};
    int cnt = 0;
    for (int i = 0; i < tet_qualities.size(); i++) {
        if (t_is_removed[i])
            continue;
        cnt++;
        if (tet_qualities[i].min_d_angle < min)
            min = tet_qualities[i].min_d_angle;
        if (tet_qualities[i].max_d_angle > max)
            max = tet_qualities[i].max_d_angle;
        // if (tet_qualities[i].asp_ratio_2 > max_asp_ratio)
            // max_asp_ratio = tet_qualities[i].asp_ratio_2;
        if (tet_qualities[i].slim_energy > max_slim_energy)
            max_slim_energy = tet_qualities[i].slim_energy;
        min_avg += tet_qualities[i].min_d_angle;
        max_avg += tet_qualities[i].max_d_angle;
        // avg_asp_ratio += tet_qualities[i].asp_ratio_2;
        avg_slim_energy += tet_qualities[i].slim_energy;

        for (int j = 0; j < 3; j++) {
            if (tet_qualities[i].min_d_angle < cmp_d_angles[j]) {
                cmp_cnt[j]++;
            }
        }
        for (int j = 0; j < 3; j++) {
            if (tet_qualities[i].max_d_angle > cmp_d_angles[j + 3]) {
                cmp_cnt[j + 3]++;
            }
        }
    }
    logger().debug("min_d_angle = {}, max_d_angle = {}, max_slim_energy = {}", min, max, max_slim_energy);
    logger().debug("avg_min_d_angle = {}, avg_max_d_angle = {}, avg_slim_energy = {}", min_avg / cnt, max_avg / cnt, avg_slim_energy / cnt);
    logger().debug("min_d_angle: <6 {};   <12 {};  <18 {}", cmp_cnt[0] / cnt, cmp_cnt[1] / cnt, cmp_cnt[2] / cnt);
    logger().debug("max_d_angle: >174 {}; >168 {}; >162 {}", cmp_cnt[5] / cnt, cmp_cnt[4] / cnt, cmp_cnt[3] / cnt);

    addRecord(MeshRecord(MeshRecord::OpType::OP_WN, time, v_ids.size(), cnt,
                         min, min_avg / cnt, max, max_avg / cnt, max_slim_energy, avg_slim_energy / cnt), args, state);

    // output unrounded vertices:
    cnt = 0;
    for (int v_id: v_ids) {
        if (!tet_vertices[v_id].is_rounded) {
            cnt++;
        }
    }
    logger().debug("{}/{} vertices are unrounded!!!", cnt, v_ids.size());
    addRecord(MeshRecord(MeshRecord::OpType::OP_UNROUNDED, -1, cnt, -1), args, state);
}

// -----------------------------------------------------------------------------

void extractSurfaceMesh(const Eigen::MatrixXd &V, const Eigen::MatrixXi &T,
    Eigen::MatrixXd &VS, Eigen::MatrixXi &FS)
{
    Eigen::VectorXi I;
    igl::boundary_facets(T, FS);
    igl::remove_unreferenced(V, FS, VS, FS, I);
}

// -----------------------------------------------------------------------------

void extractFinalTetmesh(MeshRefinement& MR,
    Eigen::MatrixXd &V_out, Eigen::MatrixXi &T_out, Eigen::VectorXd &A_out,
    const Args &args, const State &state)
{
    std::vector<TetVertex> &tet_vertices = MR.tet_vertices;
    std::vector<std::array<int, 4>> &tets = MR.tets;
    std::vector<bool> &t_is_removed = MR.t_is_removed;
    std::vector<TetQuality> &tet_qualities = MR.tet_qualities;
    int t_cnt = std::count(t_is_removed.begin(), t_is_removed.end(), false);
    double tmp_time = 0;
    // When explicitly smoothing open boundaries, the "in-out" filtering has
    // been done previously as a post-processing step of MeshRefinement.
    // Otherwise we need to tag in-out tetrahedra here.
    if (!args.smooth_open_boundary) {
        InoutFiltering IOF(tet_vertices, tets, MR.is_surface_fs, t_is_removed, state);
        igl::Timer igl_timer;
        igl_timer.start();
        t_is_removed = IOF.filter();
        t_cnt = std::count(t_is_removed.begin(), t_is_removed.end(), false);
        tmp_time = igl_timer.getElapsedTime();
        logger().info("time = {}s", tmp_time);
        logger().debug("{} tets inside!", t_cnt);
    }

    //output result
    std::vector<int> v_ids;
    for (int i = 0; i < tets.size(); i++) {
        if (t_is_removed[i])
            continue;
        for (int j = 0; j < 4; j++)
            v_ids.push_back(tets[i][j]);
    }
    std::sort(v_ids.begin(), v_ids.end());
    v_ids.erase(std::unique(v_ids.begin(), v_ids.end()), v_ids.end());
    std::unordered_map<int, int> map_ids;
    for (int i = 0; i < v_ids.size(); i++) {
        map_ids[v_ids[i]] = i;
    }

    V_out.resize(v_ids.size(), 3);
    T_out.resize(t_cnt, 4);
    A_out.resize(t_cnt);
    for (int i = 0; i < v_ids.size(); i++) {
        for (int j = 0; j < 3; j++) {
            V_out(i, + j) = tet_vertices[v_ids[i]].posf[j];
        }
    }
    int cnt = 0;
    for (int i = 0; i < tets.size(); i++) {
        if (t_is_removed[i]) {
            continue;
        }
        for (int j = 0; j < 4; j++) {
            T_out(cnt, j) = map_ids[tets[i][j]];
        }
        A_out(cnt) = tet_qualities[i].min_d_angle;
        cnt++;
    }
    logger().debug("#v = {}", V_out.rows());
    logger().debug("#t = {}", T_out.rows());

    if (args.is_quiet) {
        return;
    }
    printFinalQuality(tmp_time, tet_vertices, tets, t_is_removed, tet_qualities, v_ids, args, state);
}

// -----------------------------------------------------------------------------

// Extract ambient tet-mesh with a region tag, 0 being outside, 1 being inside
void extractRegionMesh(const MeshRefinement& MR,
    Eigen::MatrixXd &V, Eigen::MatrixXi &T, Eigen::VectorXi &R,
    const State &state)
{
    // volume mesh
    extractVolumeMesh(MR.tet_vertices, MR.tets, MR.t_is_removed, V, T);

    // surface mesh
    Eigen::MatrixXd VS;
    Eigen::MatrixXi FS;
    extractTrackedSurfaceMesh(MR.tet_vertices, MR.tets, MR.t_is_removed, MR.is_surface_fs, VS, FS, state);
    igl::write_triangle_mesh("boundary_mesh.obj", VS, FS);

    // compute inside/outside info
    Eigen::MatrixXd C;
    Eigen::VectorXd W;
    igl::barycenter(V, T, C);
    igl::winding_number(VS, FS, C, W);
    R.resize(T.rows());
    for (int t = 0; t < T.rows(); ++t) {
        R(t) = (W(t) > 0.5);
    }
}

// -----------------------------------------------------------------------------

void extractInsideMesh(
    const Eigen::MatrixXd &VI,
    const Eigen::MatrixXi &FI,
    const MeshRefinement& MR,
    Eigen::MatrixXd &V,
    Eigen::MatrixXi &T,
    const State &state)
{
    // volume mesh
    extractVolumeMesh(MR.tet_vertices, MR.tets, MR.t_is_removed, V, T);

    // surface mesh
    Eigen::MatrixXd VS = VI;
    Eigen::MatrixXi FS = FI;
    // extractTrackedSurfaceMesh(MR.tet_vertices, MR.tets, MR.t_is_removed, MR.is_surface_fs, VS, FS, state);

    // compute inside/outside info
    Eigen::MatrixXd C;
    Eigen::VectorXd W;
    igl::barycenter(V, T, C);
    igl::winding_number(VS, FS, C, W);
    int cnt = 0;
    for (int t = 0; t < T.rows(); ++t) {
        if ((W(t) > 0.5)) {
            T.row(cnt++) = T.row(t);
        }
    }
    T.conservativeResize(cnt, T.cols());

    Eigen::MatrixXd VV;
    Eigen::MatrixXi TT, F;
    Eigen::VectorXi I;
    igl::remove_unreferenced(V, T, VV, TT, I);
    V = VV;
    T = TT;
}

////////////////////////////////////////////////////////////////////////////////

// Simplify the input surface by swapping and removing edges, while staying within the envelope
double tetwild_stage_one_preprocess(
    const Eigen::MatrixXd &VI,
    const Eigen::MatrixXi &FI,
    const Args &args,
    State &state,
    GEO::Mesh &geo_sf_mesh,
    GEO::Mesh &geo_b_mesh,
    std::vector<Point_3> &m_vertices,
    std::vector<std::array<int, 3>> &m_faces)
{
    igl::Timer igl_timer;
    igl_timer.start();
    logger().info("Preprocessing...");
    if (args.user_callback) { args.user_callback(Step::Preprocess, 0.0); }
    Preprocess pp(state);
    if (!pp.init(VI, FI, geo_b_mesh, geo_sf_mesh, args)) {
        //todo: output a empty tetmesh
        PyMesh::MshSaver mSaver(state.working_dir + state.postfix + ".msh", true);
        Eigen::VectorXd oV;
        Eigen::VectorXi oT;
        oV.resize(0);
        oT.resize(0);
        mSaver.save_mesh(oV, oT, 3, mSaver.TET);
        log_and_throw("Empty mesh!");
    }
    addRecord(MeshRecord(MeshRecord::OpType::OP_INIT, 0, geo_sf_mesh.vertices.nb(), geo_sf_mesh.facets.nb()), args, state);

    m_vertices.clear();
    m_faces.clear();
    //optimize with mmgs
    if (args.use_mmgs) {
        MmgOptions opt;
        opt.hsiz = 2.0 * igl::bounding_box_diagonal(VI);
        opt.hgrad *= 2.0;
        opt.hausd = state.eps_input;
        opt.angle_detection = (args.mmg_angle_thres > 0.0);
        opt.angle_value = args.mmg_angle_thres;
        if (logger().level() == spdlog::level::trace) {
            opt.verbose = 10;
        } else if (logger().level() == spdlog::level::debug) {
            opt.verbose = 5;
        } else {
            opt.verbose = 0;
        }
        Eigen::MatrixXd VO;
        Eigen::MatrixXi FO;
        if (remesh_uniform_sf(VI, FI, VO, FO, opt)) {
            assert(VO.rows() > 0 && FO.rows() > 0);
            // igl::write_triangle_mesh("simplified.obj", VO, FO);
            m_vertices.reserve(VO.rows());
            for (int v = 0; v < VO.rows(); ++v) {
                m_vertices.emplace_back(VO(v, 0), VO(v, 1), VO(v, 2));
            }
            m_faces.reserve(FO.rows());
            for (int f = 0; f < FO.rows(); ++f) {
                m_faces.push_back({{FO(f, 0), FO(f, 1), FO(f, 2)}});
            }
        } else {
            logger().warn("mmgs didn't succeed, using TetWild's simplify procedure");
        }
    }
    if (m_vertices.empty()) {
        pp.process(geo_sf_mesh, m_vertices, m_faces, args);
    }
    double tmp_time = igl_timer.getElapsedTime();
    addRecord(MeshRecord(MeshRecord::OpType::OP_PREPROCESSING, tmp_time, m_vertices.size(), m_faces.size()), args, state);
    logger().info("time = {}s", tmp_time);
    return tmp_time;
}

// -----------------------------------------------------------------------------

// Compute an initial Delaunay triangulation of the input triangle soup
double tetwild_stage_one_delaunay(
    const Args &args,
    const State &state,
    GEO::Mesh &geo_sf_mesh,
    const std::vector<Point_3> &m_vertices,
    const std::vector<std::array<int, 3>> &m_faces,
    std::vector<Point_3> &bsp_vertices,
    std::vector<BSPEdge> &bsp_edges,
    std::vector<BSPFace> &bsp_faces,
    std::vector<BSPtreeNode> &bsp_nodes,
    std::vector<int> &m_f_tags,
    std::vector<int> &raw_e_tags,
    std::vector<std::vector<int>> &raw_conn_e4v)
{
    igl::Timer igl_timer;
    igl_timer.start();
    logger().info("Delaunay tetrahedralizing...");
    if (args.user_callback) { args.user_callback(Step::Delaunay, 0.0); }
    DelaunayTetrahedralization DT;
    m_f_tags.clear();
    raw_e_tags.clear();
    raw_conn_e4v.clear();
    DT.init(m_vertices, m_faces, m_f_tags, raw_e_tags, raw_conn_e4v);
    bsp_vertices.clear();
    bsp_edges.clear();
    bsp_faces.clear();
    bsp_nodes.clear();
    DT.tetra(m_vertices, geo_sf_mesh, bsp_vertices, bsp_edges, bsp_faces, bsp_nodes, args, state);
    logger().debug("# bsp_vertices = {}", bsp_vertices.size());
    logger().debug("# bsp_edges = {}", bsp_edges.size());
    logger().debug("# bsp_faces = {}", bsp_faces.size());
    logger().debug("# bsp_nodes = {}", bsp_nodes.size());
    logger().info("Delaunay tetrahedralization done!");
    double tmp_time = igl_timer.getElapsedTime();
    addRecord(MeshRecord(MeshRecord::OpType::OP_DELAUNEY_TETRA, tmp_time, bsp_vertices.size(), bsp_nodes.size()), args, state);
    logger().info("time = {}s", tmp_time);
    return tmp_time;
}

// -----------------------------------------------------------------------------

// Match faces of the Delaunay tetrahedralization with faces from the input mesh
double tetwild_stage_one_mc(
    const Args &args,
    const State &state,
    MeshConformer &MC)
{
    igl::Timer igl_timer;
    igl_timer.start();
    logger().info("Divfaces matching...");
    if (args.user_callback) { args.user_callback(Step::FaceMatching, 0.0); }
    MC.match(args);
    logger().info("Divfaces matching done!");
    double tmp_time = igl_timer.getElapsedTime();
    addRecord(MeshRecord(MeshRecord::OpType::OP_DIVFACE_MATCH, tmp_time, MC.bsp_vertices.size(), MC.bsp_nodes.size()), args, state);
    logger().info("time = {}s", tmp_time);
    return tmp_time;
}

// -----------------------------------------------------------------------------

// Compute BSP partition of the domain
double tetwild_stage_one_bsp(
    const Args &args,
    const State &state,
    MeshConformer &MC)
{
    igl::Timer igl_timer;
    igl_timer.start();
    logger().info("BSP subdivision ...");
    if (args.user_callback) { args.user_callback(Step::BSP, 0.0); }
    BSPSubdivision BS(MC);
    BS.init();
    BS.subdivideBSPNodes(args);
    logger().debug("Output: ");
    logger().debug("# node = {}", MC.bsp_nodes.size());
    logger().debug("# face = {}", MC.bsp_faces.size());
    logger().debug("# edge = {}", MC.bsp_edges.size());
    logger().debug("# vertex = {}", MC.bsp_vertices.size());
    logger().info("BSP subdivision done!");
    double tmp_time = igl_timer.getElapsedTime();
    addRecord(MeshRecord(MeshRecord::OpType::OP_BSP, tmp_time, MC.bsp_vertices.size(), MC.bsp_nodes.size()), args, state);
    logger().info("time = {}s", tmp_time);
    return tmp_time;
}

// -----------------------------------------------------------------------------

// Compute an initial tetrahedral mesh from the BSP partition
double tetwild_stage_one_tetra(
    const Args &args,
    const State &state,
    MeshConformer &MC,
    const std::vector<int> &m_f_tags,
    const std::vector<int> &raw_e_tags,
    const std::vector<std::vector<int>> &raw_conn_e4v,
    std::vector<TetVertex> &tet_vertices,
    std::vector<std::array<int, 4>> &tet_indices,
    std::vector<std::array<int, 4>> &is_surface_facet)
{
    igl::Timer igl_timer;
    igl_timer.start();
    logger().info("Tetrehedralizing ...");
    if (args.user_callback) { args.user_callback(Step::Tetra, 0.0); }
    SimpleTetrahedralization ST(args, state, MC);
    tet_vertices.clear();
    tet_indices.clear();
    is_surface_facet.clear();
    ST.tetra(tet_vertices, tet_indices);
    ST.labelSurface(m_f_tags, raw_e_tags, raw_conn_e4v, tet_vertices, tet_indices, is_surface_facet);
    ST.labelBbox(tet_vertices, tet_indices);
    if (!state.is_mesh_closed) {
        //if input is an open mesh
        ST.labelBoundary(tet_vertices, tet_indices, is_surface_facet);
    }
    logger().debug("# tet_vertices = {}", tet_vertices.size());
    logger().debug("# tets = {}", tet_indices.size());
    logger().info("Tetrahedralization done!");
    double tmp_time = igl_timer.getElapsedTime();
    addRecord(MeshRecord(MeshRecord::OpType::OP_SIMPLE_TETRA, tmp_time, tet_vertices.size(), tet_indices.size()), args, state);
    logger().info("time = {}s", tmp_time);
    return tmp_time;
}

////////////////////////////////////////////////////////////////////////////////

void tetwild_stage_one(
    const Eigen::MatrixXd &VI,
    const Eigen::MatrixXi &FI,
    const Args &args,
    State &state,
    GEO::Mesh &geo_sf_mesh,
    GEO::Mesh &geo_b_mesh,
    std::vector<TetVertex> &tet_vertices,
    std::vector<std::array<int, 4>> &tet_indices,
    std::vector<std::array<int, 4>> &is_surface_facet)
{
    igl::Timer igl_timer;
    double tmp_time = 0;
    double sum_time = 0;

    //preprocess
    std::vector<Point_3> m_vertices;
    std::vector<std::array<int, 3>> m_faces;
    sum_time += tetwild_stage_one_preprocess(VI, FI, args, state, geo_sf_mesh, geo_b_mesh, m_vertices, m_faces);

    //delaunay tetrahedralization
    std::vector<Point_3> bsp_vertices;
    std::vector<BSPEdge> bsp_edges;
    std::vector<BSPFace> bsp_faces;
    std::vector<BSPtreeNode> bsp_nodes;
    std::vector<int> m_f_tags;
    std::vector<int> raw_e_tags;
    std::vector<std::vector<int>> raw_conn_e4v;
    sum_time += tetwild_stage_one_delaunay(args, state, geo_sf_mesh, m_vertices, m_faces,
        bsp_vertices, bsp_edges, bsp_faces, bsp_nodes, m_f_tags, raw_e_tags, raw_conn_e4v);

    //mesh conforming
    MeshConformer MC(m_vertices, m_faces, bsp_vertices, bsp_edges, bsp_faces, bsp_nodes);
    sum_time += tetwild_stage_one_mc(args, state, MC);

    //bsp subdivision
    sum_time += tetwild_stage_one_bsp(args, state, MC);

    //simple tetrahedralization
    sum_time += tetwild_stage_one_tetra(args, state, MC, m_f_tags, raw_e_tags, raw_conn_e4v,
        tet_vertices, tet_indices, is_surface_facet);

    logger().info("Total time for the first stage = {}s", sum_time);
}

// -----------------------------------------------------------------------------

void tetwild_stage_two(
    const Eigen::MatrixXd &VI,
    const Eigen::MatrixXi &FI,
    Args &args,
    State &state,
    GEO::Mesh &geo_sf_mesh,
    GEO::Mesh &geo_b_mesh,
    std::vector<TetVertex> &tet_vertices,
    std::vector<std::array<int, 4>> &tet_indices,
    std::vector<std::array<int, 4>> &is_surface_facet,
    Eigen::MatrixXd &VO,
    Eigen::MatrixXi &TO,
    Eigen::VectorXd &AO)
{
    //init
    logger().info("Refinement initializing...");
    if (args.user_callback) { args.user_callback(Step::Optimize, 0.0); }
    MeshRefinement MR(geo_sf_mesh, geo_b_mesh, args, state);
    MR.tet_vertices = std::move(tet_vertices);
    MR.tets = std::move(tet_indices);
    MR.is_surface_fs = std::move(is_surface_facet);
    MR.prepareData();
    logger().info("Refinement initialization done!");

    //improvement
    MR.refine(state.ENERGY_AMIPS);

    //post-optimization with mmg3d
    if (args.use_mmg3d) {
        MmgOptions opt;
        opt.hsiz = state.initial_edge_len;
        opt.hausd = state.eps_input;
        opt.angle_detection = (args.mmg_angle_thres > 0.0);
        opt.angle_value = args.mmg_angle_thres;
        if (logger().level() == spdlog::level::trace) {
            opt.verbose = 10;
        } else if (logger().level() == spdlog::level::debug) {
            opt.verbose = 5;
        } else {
            opt.verbose = 0;
        }
        Eigen::MatrixXi FO;
        Eigen::VectorXi R;
        // extractRegionMesh(MR, VO, TO, R, state);
        extractInsideMesh(VI, FI, MR, VO, TO, state);
        // igl::writeMESH("before_mmg.mesh", VO, TO, FO);
        logger().debug("mesh quality ok: {}", isMeshQualityOk(VO, TO));
        logger().debug("volume ok: {}", checkVolume(VO, TO));
        if (remesh_uniform_3d(VO, TO, R, VO, FO, TO, R, opt)) {
            assert(VO.rows() > 0 && FO.rows() > 0);
            // filterRegion(VO, TO, R, 1, VO, TO);
            AO.resize(TO.rows());
            AO.setZero(); //clear values
        } else {
            logger().warn("mmg3d failed to optimize the mesh, reverting to TetWild");
            args.use_mmg3d = false;

            //improvement
            MR.refine(state.ENERGY_AMIPS, {{true, true, true, true}}, false, true);
        }
    } else {
        extractFinalTetmesh(MR, VO, TO, AO, args, state); //do winding number and output the tetmesh
    }
}

////////////////////////////////////////////////////////////////////////////////

void tetrahedralization(const Eigen::MatrixXd &VI, const Eigen::MatrixXi &FI,
                        Eigen::MatrixXd &VO, Eigen::MatrixXi &TO, Eigen::VectorXd &AO,
                        const Args &args_)
{
    Args args = args_;
    igl::Timer igl_timer;
    igl_timer.start();

    ////pipeline
    State state(args, VI);
    GEO::Mesh geo_sf_mesh;
    GEO::Mesh geo_b_mesh;
    std::vector<TetVertex> tet_vertices;
    std::vector<std::array<int, 4>> tet_indices;
    std::vector<std::array<int, 4>> is_surface_facet;

    /// STAGE 1
    tetwild_stage_one(VI, FI, args, state, geo_sf_mesh, geo_b_mesh,
        tet_vertices, tet_indices, is_surface_facet);

    /// STAGE 2
    tetwild_stage_two(VI, FI, args, state, geo_sf_mesh, geo_b_mesh,
        tet_vertices, tet_indices, is_surface_facet, VO, TO, AO);

    double total_time = igl_timer.getElapsedTime();
    logger().info("Total time for all stages = {}s", total_time);
}

} // namespace tetwild
