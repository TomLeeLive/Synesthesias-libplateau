﻿#include <stdexcept>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <sys/stat.h>
#include <filesystem>

#if defined(_WIN32)
#include <direct.h>
#endif

#include <citygml/citygml.h>
#include <citygml/citymodel.h>
#include <citygml/geometry.h>
#include <citygml/polygon.h>
#include <citygml/texture.h>

#include "obj_writer.h"
#include "polar_to_plane_cartesian.h"

void ObjWriter::write(const std::string& obj_file_path, const citygml::CityModel& city_model, const std::string& gml_file_path) {

    gml_file_path_ = gml_file_path;
    obj_file_path_ = obj_file_path;
    unsigned int v_offset = 0, t_offset = 0;

    dll_logger_->log(DllLogLevel::LL_INFO, "Convert Start.\ngml path = " + gml_file_path + "\nto " + obj_file_path );

    ofs_ = std::ofstream(obj_file_path_);
    if (!ofs_.is_open()) {
        dll_logger_->throwException(std::string("Failed to open stream of obj path : ") + obj_file_path_);
    }

    const size_t dir_i = obj_file_path_.find_last_of("/");
    const size_t file_i = obj_file_path_.find_last_of(".");
    const std::string mat_file_path =  obj_file_path_.substr(0, file_i) + ".mtl";

    std::string file_name_without_extension;
    if (dir_i == std::string::npos) {
        file_name_without_extension = obj_file_path_.substr(0, file_i);
    } else {
        file_name_without_extension = obj_file_path_.substr(dir_i + 1, file_i - dir_i - 1);
    }
    std::string mat_file_name = file_name_without_extension + ".mtl";

    ofs_mat_ = std::ofstream(mat_file_path);
    if (!ofs_mat_.is_open()) {
        ofs_.close();
        dll_logger_->throwException(std::string("Failed to open stream of material path : ") + mat_file_path);
    }

    ofs_mat_ << "newmtl obj_def_mat" << std::endl;
    ofs_mat_ << "Kd 0.5 0.5 0.5" << std::endl << std::endl;

    ofs_ << std::fixed << std::setprecision(6);
    const auto rc = city_model.getNumRootCityObjects();
    dll_logger_->log(DllLogLevel::LL_INFO, "NumRootCityObjects: " + std::to_string(rc));
    ofs_ << "mtllib " << mat_file_name << std::endl;

    // メッシュを1つに結合する設定なら、その唯一のメッシュの名称を設定します。
    if(mesh_granularity_ == MeshGranularity::PerCityModelArea){
        ofs_ << "g " << file_name_without_extension << std::endl;
    }

    for (const auto& root_object : city_model.getRootCityObjects()) {
        const std::string rbid = root_object->getAttribute(u8"建物ID");
        dll_logger_->log(DllLogLevel::LL_TRACE, "RootID : " + root_object->getId());
        if (mesh_granularity_ == MeshGranularity::PerPrimaryFeatureObject && !rbid.empty()) {
            ofs_ << "g " << root_object->getId() << std::endl;
        }

        const auto cc = root_object->getChildCityObjectsCount();

        //for LOD1
        if (cc == 0) {
            const auto& target_object = *root_object;

            writeCityObject(target_object, v_offset, t_offset, true);
        }

        dll_logger_->log(DllLogLevel::LL_TRACE, "ChildCityObjectsCount : " + std::to_string(cc));
        for (unsigned int i = 0; i < cc; i++) {
            const auto& target_object = root_object->getChildCityObject(i);

            processChildCityObject(target_object, v_offset, t_offset);
        }
    }
    ofs_.close();
    ofs_mat_.close();

    if(!anyVertexExists(obj_file_path_)){
        std::filesystem::remove(obj_file_path_);
        std::filesystem::remove(mat_file_path);
        dll_logger_->throwException("No vertex found. Deleting output obj & mat.");
    }
}

void ObjWriter::processChildCityObject(const citygml::CityObject& target_object, unsigned int& v_offset, unsigned int& t_offset) {
    const std::string cbid = target_object.getAttribute("建物ID");
    if (!cbid.empty()) {
        dll_logger_->log(DllLogLevel::LL_TRACE, "建物ID : " + cbid);
    }
    if (mesh_granularity_ == MeshGranularity::PerAtomicFeatureObject || (!cbid.empty() && mesh_granularity_ != MeshGranularity::PerCityModelArea) ) {
        ofs_ << "g " << target_object.getId() << std::endl;
    }
    dll_logger_->log(DllLogLevel::LL_TRACE, "ChildID : " + target_object.getId());

    writeCityObject(target_object, v_offset, t_offset, false);

    const auto cc = target_object.getChildCityObjectsCount();
    if (cc != 0) {
        dll_logger_->log(DllLogLevel::LL_TRACE, "grandChildCityObjectsCount : " + std::to_string(cc));
        for (unsigned int i = 0; i < cc; i++) {
            const auto& new_target_object = target_object.getChildCityObject(i);

            processChildCityObject(new_target_object, v_offset, t_offset);
        }
    }
}

unsigned int ObjWriter::writeVertices(const std::vector<TVec3d>& vertices) {
    int cnt = 0;
    std::for_each(vertices.cbegin(), vertices.cend(), [&](const TVec3d& v) {
        double xyz[3];
        for (int i = 0; i < 3; i++) xyz[i] = v[i];
        polar_to_plane_cartesian().convert(xyz);
        for (int i = 0; i < 3; i++) xyz[i] -= ref_point_[i];
        if (axes_ == AxesConversion::WNU) {
            ofs_ << "v " << xyz[0] << " " << xyz[1] << " " << xyz[2] << std::endl;
        }
        else if (axes_ == AxesConversion::RUF) {
            ofs_ << "v " << -xyz[0] << " " << xyz[2] << " " << xyz[1] << std::endl;
        } 
        else {
            throw std::runtime_error("Unknown axes type.");
        }
        cnt++;
    });
    return cnt;
}

unsigned int ObjWriter::writeUVs(const std::vector<TVec2f>& uvs) {
    int cnt = 0;
    std::for_each(uvs.cbegin(), uvs.cend(), [&](const TVec2f& uv) {
        ofs_ << "vt " << uv.x << " " << uv.y << std::endl;
        cnt++;
    });
    return cnt;
}

void ObjWriter::writeIndices(const std::vector<unsigned int>& indices, unsigned int ix_offset, unsigned int tx_offset, bool tex_flg) {
    int i = 0;
    for (auto itr = indices.begin(); itr != indices.end(); itr++) {
        if (i == 0) ofs_ << "f ";
        if (tex_flg) {
            ofs_ << *itr + 1 + ix_offset << "/" << *itr + 1 + tx_offset << " ";
        } else {
            ofs_ << *itr + 1 + ix_offset << " ";
        }
        i++;
        if (i == 3) {
            i = 0;
            ofs_ << std::endl;
        }
    }
}

void ObjWriter::writeMaterial(const std::string& tex_path) {
    size_t path_i = tex_path.find_last_of("/") + 1;
    size_t ext_i = tex_path.find_last_of(".");
    std::string mat_name = tex_path.substr(path_i, ext_i - path_i);

    bool newmat_flg = true;
    for (const auto& item : mat_list_) {
        if (item == mat_name) {
            newmat_flg = false;
            break;
        }
    }

    ofs_ << "usemtl " << mat_name << std::endl;

    if (newmat_flg) {
        ofs_mat_ << "newmtl " << mat_name << std::endl;
        ofs_mat_ << "map_Kd ./" << tex_path << std::endl << std::endl;
        mat_list_.push_back(mat_name);

        std::string path_from = gml_file_path_.substr(0, gml_file_path_.find_last_of("/") + 1) + tex_path;
        std::string path_to;

        size_t dir_i = obj_file_path_.find_last_of("/");
        if (dir_i == std::string::npos) {
            path_to = tex_path;
        } else {
            path_to = obj_file_path_.substr(0, dir_i + 1) + tex_path;
        }

        struct stat statBuf;
        std::string to_dir = path_to.substr(0, path_to.find_last_of("/"));
        if (stat(to_dir.c_str(), &statBuf) != 0) {
            int mkdirResult;
#if defined(_WIN32)
            mkdirResult = _mkdir(to_dir.c_str());
#else
            mkdirResult = mkdir(to_dir.c_str(), 0777);
#endif
            if (mkdirResult != 0) {
                closeStreams();
                dll_logger_->throwException(std::string("Failed to make directory : ") + to_dir);
            }
        }
        std::ifstream ifstr(path_from, std::ios::binary);
        if (!ifstr.is_open()) {
            closeStreams();
            dll_logger_->throwException(std::string("Failed to open stream of material source path : ") + path_from);
        }
        std::ofstream ofstr(path_to, std::ios::binary);
        if (!ofstr.is_open()) {
            closeStreams();
            dll_logger_->throwException(std::string("Failed to open stream of material destination path : ") + path_to);
        }
        ofstr << ifstr.rdbuf();
    }
}

void ObjWriter::setDestAxes(AxesConversion value) {
    axes_ = value;
}

AxesConversion ObjWriter::getDestAxes() const {
    return axes_;
}

void ObjWriter::setValidReferencePoint(const citygml::CityModel& city_model) {
    auto lower_bound = city_model.getEnvelope().getLowerBound();
    auto upper_bound = city_model.getEnvelope().getUpperBound();

    polar_to_plane_cartesian().convert(lower_bound);
    polar_to_plane_cartesian().convert(upper_bound);
    
    ref_point_[0] = (lower_bound.x + upper_bound.x)/2.0;
    ref_point_[1] = (lower_bound.y + upper_bound.y) / 2.0;
    ref_point_[2] = lower_bound.z;

    dll_logger_->log(DllLogLevel::LL_TRACE, "Set ReferencePoint @ " + std::to_string(ref_point_[0]) + ", " + std::to_string(ref_point_[1]) + ", " + std::to_string(ref_point_[2]));
}

void ObjWriter::getReferencePoint(double xyz[]) const{
    for (int i = 0; i < 3; i++) xyz[i] = ref_point_[i];
}

void ObjWriter::setReferencePoint(const double xyz[]) {
    for (int i = 0; i < 3; i++) ref_point_[i] = xyz[i];
    dll_logger_->log(DllLogLevel::LL_TRACE, "Set ReferencePoint @ " + std::to_string(ref_point_[0]) + ", " + std::to_string(ref_point_[1]) + ", " + std::to_string(ref_point_[2]));
}

void ObjWriter::writeCityObject(const citygml::CityObject& target_object, unsigned int& v_offset, unsigned int& t_offset, bool recursive_flg) {
    const auto gc = target_object.getGeometriesCount();
    dll_logger_->log(DllLogLevel::LL_TRACE, "GeometriesCount = " + std::to_string(gc));
    for (unsigned int j = 0; j < gc; j++) {
        if (target_object.getGeometry(j).getLOD() == 0) {
            dll_logger_->log(DllLogLevel::LL_TRACE, "Found LOD0 Geometry. Skipped it.");
            continue;
        }
        writeGeometry(target_object.getGeometry(j), v_offset, t_offset, recursive_flg);
    }
}

void ObjWriter::writeGeometry(const citygml::Geometry& target_geometry, unsigned int& v_offset, unsigned int& t_offset, bool recursive_flg) {
    const auto pc = target_geometry.getPolygonsCount();
    if(pc <= 0){
        dll_logger_->log(CityGMLLogger::LOGLEVEL::LL_INFO, "Polygon Count is zero on the target_geometry.");
    }
    dll_logger_->log(DllLogLevel::LL_TRACE, "PolygonsCount = " + std::to_string(pc));
    for (unsigned int k = 0; k < pc; k++) {
        const auto v_cnt = writeVertices(target_geometry.getPolygon(k)->getVertices());
        if(v_cnt <= 0){
            dll_logger_->log(CityGMLLogger::LOGLEVEL::LL_INFO, "vertices count is zero in the polygon.");
        }

        const auto citygmlTex = target_geometry.getPolygon(k)->getTextureFor("rgbTexture");
        bool tex_flg = false;
        unsigned int t_cnt = 0;
        if (citygmlTex) {
            tex_flg = true;
            t_cnt = writeUVs(target_geometry.getPolygon(k)->getTexCoordsForTheme("rgbTexture", true));
            writeMaterial(citygmlTex->getUrl());
        } else {
            ofs_ << "usemtl obj_def_mat" << std::endl;
        }

        writeIndices(target_geometry.getPolygon(k)->getIndices(), v_offset, t_offset, tex_flg);
        v_offset += v_cnt;
        t_offset += t_cnt;
    }

    const auto cgc = target_geometry.getGeometriesCount();
    if (cgc != 0 && recursive_flg) {
        dll_logger_->log(DllLogLevel::LL_TRACE, "childGeometriesCount : " + std::to_string(cgc));
        for (unsigned int i = 0; i < cgc; i++) {
            const auto& new_target_geometry = target_geometry.getGeometry(i);

            writeGeometry(new_target_geometry, v_offset, t_offset, recursive_flg);
        }
    }
}

void ObjWriter::setMeshGranularity(MeshGranularity value) {
    mesh_granularity_ = value;
}

void ObjWriter::closeStreams(){
    if(ofs_.is_open()) ofs_.close();
    if(ofs_mat_.is_open()) ofs_mat_.close();
}

bool ObjWriter::anyVertexExists(const std::string& obj_path){
    auto obj_stream = std::ifstream(obj_path);
    if(!obj_stream.is_open()){
        dll_logger_->throwException("Output obj file is not found.");
    }
    const std::string search_prefix = "v ";
    std::string line;
    while(std::getline(obj_stream, line)){
        if(line.size() > search_prefix.size() &&
           std::equal(std::begin(search_prefix), std::end(search_prefix), std::begin(line))){
            obj_stream.close();
            return true;
        }
    }
    obj_stream.close();
    return false;
}

MeshGranularity ObjWriter::getMeshGranularity() const {
    return mesh_granularity_;
}

const PlateauDllLogger * ObjWriter::getLogger() const {
    return dll_logger_.get();
}