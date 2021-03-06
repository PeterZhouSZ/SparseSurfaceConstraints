// Object2SDF.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <string>
#include <fstream>
#include <array>
#include <vector>

#include <cinder/app/AppBase.h>
#include <cinder/TriMesh.h>
#include <cinder/ObjLoader.h>

#include <ObjectLoader.h>

using namespace glm;

struct WorldGrid
{
    //The size of each voxel as a fraction of 1 world unit
    //voxelResolution=4 -> voxel side length = 1/4 world units
    int voxelResolution;

    //The offsets in voxels from where the grid starts (lower corner, -x, -y, -z)
    glm::ivec3 offset;

    //The size of the grid in voxels
    glm::ivec3 size;

    std::vector<float> data;

    WorldGrid(int voxelResolution, const ivec3& offset, const ivec3& size)
        : voxelResolution(voxelResolution)
        , offset(offset)
        , size(size)
    {
        data.resize(size.x * size.y * size.z);
    }

    ivec3 indexToPos(int idx) const
    {
        int k = idx / (size.x*size.y);
        int j = (idx - (k * size.x*size.y)) / size.x;
        int i = idx - size.x * (j + size.y * k);
        return ivec3(i, j, k);
    }
    vec3 toWorld(ivec3 ijk) const 
    {
        return vec3(ijk + offset) / vec3(voxelResolution);
    }
    int numCells() const { return data.size(); }
};

cinder::TriMeshRef loadObj(const std::string& path)
{
    std::cout << "Loading \"" << path << "\"" << std::endl;
    cinder::ObjLoader loader(cinder::loadFile(path));
    cinder::TriMeshRef mesh = cinder::TriMesh::create(loader);
	//cinder::TriMeshRef mesh = ObjectLoader::loadCustomObj(path);
    std::cout << "Mesh \"" << path << "\" loaded,\nit contains " << mesh->getNumVertices() << " vertices and " << mesh->getNumTriangles() << " triangles" << std::endl;
    return mesh;
}

vec3 closesPointOnTriangle(const std::array<vec3, 3> triangle, const vec3 &sourcePosition);
float pointTriangleDistance(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 pos)
{
    vec3 p = closesPointOnTriangle({ a, b, c }, pos);
    float dist = length(p - pos);
    return dist;
}

int intersect_triangle(double orig[3], double dir[3], double vert0[3], double vert1[3], double vert2[3], double *t, double *u, double *v);
bool pointTriangleIntersection(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 pos)
{
    double orig[3] = { pos.x, pos.y, pos.z };
    double dir[3] = { 1, 0,0 };
    double vert0[3] = { a.x, a.y, a.z };
    double vert1[3] = { b.x, b.y, b.z };
    double vert2[3] = { c.x, c.y, c.z };
    double u, v, t;
    if (intersect_triangle(orig, dir, vert0, vert1, vert2, &t, &u, &v))
    {
        if (t >= 0) return true;
    }
    return false;
}

WorldGrid createGrid(cinder::TriMeshRef mesh, int voxelResolution, int sideLength, int border, float& scale)
{
    vec3 minPos(0, 0, 0);
    vec3 maxPos(0, 0, 0);
    for (size_t i = 0; i<mesh->getNumVertices(); ++i)
    {
        vec3 pos = mesh->getPositions<3>()[i];
        minPos = glm::min(minPos, pos);
        maxPos = glm::max(maxPos, pos);
    }
    std::cout << "minPos=" << minPos << std::endl;
    std::cout << "maxPos=" << maxPos << std::endl;
    float maxLength = std::max({ maxPos.x - minPos.x, maxPos.y - minPos.y, maxPos.z - minPos.z });
    std::cout << "max side length: " << maxLength << ", required: " << sideLength << std::endl;

    scale = sideLength>0 ? sideLength / maxLength : 1;
    std::cout << "Scaling: " << scale << std::endl;

    ivec3 size = ivec3(round(vec3(voxelResolution * scale) * (maxPos - minPos))) + 2 * ivec3(border);
    ivec3 offset = ivec3(round(vec3(voxelResolution * scale) * minPos)) - ivec3(border);
    std::cout << "Grid size: " << size << ", offset: " << offset << std::endl;
    return WorldGrid(voxelResolution, offset, size);
}

void fillGrid(WorldGrid& grid, cinder::TriMeshRef mesh, const float scaling)
{
    std::cout << "Fill grid" << std::endl;
    const int numTris = mesh->getNumTriangles();
    const int numCells = grid.numCells();
#pragma omp parallel for
    for (int idx = 0; idx<numCells; ++idx)
    {
        vec3 pos = grid.toWorld(grid.indexToPos(idx));
        pos *= grid.voxelResolution;
        float dist = std::numeric_limits<float>::max();
        int numIntersections = 0;
		bool inside = false;
        for (int i=0; i<numTris; ++i)
        {
            vec3 a, b, c;
            mesh->getTriangleVertices(i, &a, &b, &c);
            a *= scaling * grid.voxelResolution;
            b *= scaling * grid.voxelResolution;
            c *= scaling * grid.voxelResolution;
            float d = pointTriangleDistance(a, b, c, pos);
			if (d < dist) {
				dist = d;
				vec3 normal1 = cross(a - c, b - c);
				vec3 normal2 = pos - a;
				inside = dot(normal1, normal2) > 0;
			}
            if (pointTriangleIntersection(a, b, c, pos)) 
				numIntersections++;
        }
		//std::cout << "(" << pos << ") -> " << numIntersections << "\n";
        if (numIntersections % 2 == 1) dist = -dist;
		//if (numIntersections % 4 != 0) dist = -dist;
		//if (inside) dist = -dist;
        grid.data[idx] = dist;
    }
    std::cout << "Done" << std::endl;
}

void saveGrid(const WorldGrid& grid, const std::string& file)
{
    std::ofstream o(file, std::ios::out | std::ios::binary);
    o.write((char*)(&grid.voxelResolution), sizeof(int));
    o.write((char*)(&grid.offset), 3 * sizeof(int));
    o.write((char*)(&grid.size), 3 * sizeof(int));
    o.write((char*)grid.data.data(), sizeof(float)*grid.data.size());
    o.close();
}

void saveHighResMesh(const WorldGrid& grid, const float scaling, const std::string& input, const std::string& output)
{
	cinder::TriMeshRef imesh = loadObj(input);
	ci::TriMesh::Format format;
	format = format.positions().normals().colors(4).texCoords1(3);
	if (imesh->hasTexCoords0())
		format = format.texCoords0(2);
	cinder::TriMeshRef omesh = ci::TriMesh::create(format);

	//copy vertices
	for (int i = 0; i < imesh->getNumVertices(); ++i) {
		glm::vec3 pos = imesh->getPositions<3>()[i] * scaling;
		omesh->appendPosition(pos);
		if (imesh->hasNormals())
			omesh->appendNormal(imesh->getNormals()[i]);
		if (imesh->hasColors())
			omesh->appendColorRgba(imesh->getColors<4>()[i]);
		if (imesh->hasTexCoords0())
			omesh->appendTexCoord0(imesh->getTexCoords0<2>()[i]);
		//convert position to cell index
		glm::vec3 cellpos = (pos * glm::vec3(grid.voxelResolution)) - glm::vec3(grid.offset);
		if (cellpos.x < 0 || cellpos.y < 0 || cellpos.z < 0 || cellpos.x >= grid.size.x - 1 || cellpos.y >= grid.size.y - 1 || cellpos.z >= grid.size.z - 1)
			std::cerr << "Out of bounds! pos=" << pos << ", cellpos=" << cellpos << ", resolution=" << grid.voxelResolution << ", grid offset=" << grid.offset << ", grid size=" << grid.size << std::endl;
		omesh->appendTexCoord1(cellpos);
	}

	//copy indices
	omesh->getIndices().insert(omesh->getIndices().end(), imesh->getIndices().begin(), imesh->getIndices().end());
	ObjectLoader::saveCustomObj(omesh, output);
}

//void testDistance(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 pos)
//{
//    std::cout << "a=" << a << ", b=" << b << ", c=" << c << ", pos=" << pos << " -> dist=" << pointTriangleDistance(a, b, c, pos) << std::endl;
//}

int main()
{
    //input
#if 0
    std::string file = "models/dragon/dragon_vrip_res3.obj";
	std::string fileHigh = "models/dragon/dragon_vrip.obj";
	std::string outputFile = "models/dragon/dragon_medium";
    int voxelResolution = 10;
    int sideLength = 5;
#elif 1
	std::string file = "models/dragon/dragon_vrip_res3_modified5.obj";
	std::string fileHigh = "models/dragon/dragon_vrip_res3_modified5.obj";
	std::string outputFile = "models/dragon/dragon_modified_rot";
	int voxelResolution = 6;
	int sideLength = 5;
#elif 0
	std::string file = "models/dragon/dragon_vrip_res3.obj";
	std::string fileHigh = "models/dragon/dragon_vrip.obj";
	std::string outputFile = "models/dragon/dragon_small";
	int voxelResolution = 5;
	int sideLength = 5;
#elif 0
	std::string file = "../../InputData/Bottle/Baby_bottle2_lowres.obj";
	std::string fileHigh = "../../InputData/Bottle/Baby_bottle2.obj";
	std::string outputFile = "../../InputData/Bottle/Bottle";
	int voxelResolution = 120;
	int sideLength = 2;
#elif 0
	std::string file = "../../InputData/DragonHead/Dragon-head_obj1.OBJ";
	std::string fileHigh = "../../InputData/DragonHead/Dragon-head_obj1.OBJ";
	std::string outputFile = "../../InputData/DragonHead/Dragon";
	int voxelResolution = 40;
	int sideLength = 2;
#elif 0
	std::string file = "../../InputData/Tree/tree_high.obj";
	std::string fileHigh = "../../InputData/Tree/tree_high.obj";
	std::string outputFile = "../../InputData/Tree/TreeHighRes";
	int voxelResolution = 40;
	int sideLength = 2;
#elif 0
	std::string file = "../../InputData/Tree/tree_high_rotated.obj";
	std::string fileHigh = "../../InputData/Tree/tree_high_rotated.obj";
	std::string outputFile = "../../InputData/Tree/TreeRotated";
	int voxelResolution = 30;
	int sideLength = 2;
#elif 0
	std::string file = "../../InputData/Teddy/teddy02_watertight_small.obj";
	std::string fileHigh = "../../InputData/Teddy/teddy02_watertight.obj";
	std::string outputFile = "../../InputData/Teddy/Teddy";
	int voxelResolution = 20;
	int sideLength = 3;
#elif 0
	std::string file = "../../InputData/Teddy/teddy04_small.obj";
	std::string fileHigh = "../../InputData/Teddy/teddy04.obj";
	std::string outputFile = "../../InputData/Teddy/Teddy4";
	int voxelResolution = 20;
	int sideLength = 3;
#elif 0
	std::string file = "../../InputData/Teddy/teddy04-tex-small.obj";
	std::string fileHigh = "../../InputData/Teddy/teddy04-tex.obj";
	std::string outputFile = "../../InputData/Teddy/Teddy4-tex";
	int voxelResolution = 20;
	int sideLength = 3;
#elif 0
	std::string file = "../../InputData/Teddy60fps_4/groundTruth/Teddy_small_tex.obj";
	std::string fileHigh = "../../InputData/Teddy60fps_4/groundTruth/Teddy_small_tex.obj";
	std::string outputFile = "../../InputData/Teddy60fps_4/groundTruth/Teddy_small_tex";
	int voxelResolution = 64;
	int sideLength = -1;
#elif 0
	std::string file = "../../InputData/PillowRamp/groundTruth/Pillow_02_small.obj";
	std::string fileHigh = "../../InputData/PillowRamp/groundTruth/Pillow_02.obj";
	std::string outputFile = "../../InputData/PillowRamp/groundTruth/Pillow";
	int voxelResolution = 48;
	int sideLength = -1;
#elif 1
	std::string file = "../../InputData/PillowFlat/groundTruth/pillow_low.obj";
	std::string fileHigh = "../../InputData/PillowFlat/groundTruth/pillow_high.obj";
	std::string outputFile = "../../InputData/PillowFlat/groundTruth/Pillow";
	int voxelResolution = 48;
	int sideLength = -1;
#elif 0
	std::string file = "models/bottle1/bottle.obj";
	std::string fileHigh = "models/bottle1/bottle.obj";
	std::string outputFile = "models/bottle1/bottle";
	int voxelResolution = 30;
	int sideLength = 1;
#else
    std::string file = "models/bunny/bun_zipper_res2.obj";
	std::string fileHigh = "models/bunny/bun_zipper.obj";
	std::string outputFile = "models/bunny/bun_zipper_res2.obj";
    int voxelResolution = 15;
    int sideLength = 5;
#endif
    int border = 3;

    cinder::TriMeshRef mesh = loadObj(file);

    float scaling;
    WorldGrid grid = createGrid(mesh, voxelResolution, sideLength, border, scaling);

    fillGrid(grid, mesh, scaling);
    //std::cout << "Grid data:\n" << std::setw(5) << std::setprecision(2);
    //for (int i = 0; i < grid.numCells(); ++i) std::cout << " " << grid.data[i];
    //std::cout << std::endl;

    saveGrid(grid, outputFile + ".sdf");
    std::cout << "Grid saved" << std::endl;

	saveHighResMesh(grid, scaling, fileHigh, outputFile + ".sdf.obj");
	std::cout << "High-Res mesh connected to the grid saved" << std::endl;

    //testDistance(vec3(0, 0, 0), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0.2, 0.2, 0.5));
    //testDistance(vec3(0, 0, 0), vec3(1, 0, 0), vec3(0, 1, 0), vec3(-0.2, 0.2, 0.5));
    //testDistance(vec3(0, 0, 0), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0.2, -0.2, -0.5));

    system("pause");
    return 0;
}






vec3 closesPointOnTriangle(const std::array<vec3, 3> triangle, const vec3 &sourcePosition)
{
    vec3 edge0 = triangle[1] - triangle[0];
    vec3 edge1 = triangle[2] - triangle[0];
    vec3 v0 = triangle[0] - sourcePosition;

    float a = dot(edge0, edge0);
    float b = dot(edge0, edge1);
    float c = dot(edge1, edge1);
    float d = dot(edge0, v0);
    float e = dot(edge1, v0);

    float det = a * c - b * b;
    float s = b * e - c * d;
    float t = b * d - a * e;

    if (s + t < det)
    {
        if (s < 0.f)
        {
            if (t < 0.f)
            {
                if (d < 0.f)
                {
                    s = clamp(-d / a, 0.f, 1.f);
                    t = 0.f;
                }
                else
                {
                    s = 0.f;
                    t = clamp(-e / c, 0.f, 1.f);
                }
            }
            else
            {
                s = 0.f;
                t = clamp(-e / c, 0.f, 1.f);
            }
        }
        else if (t < 0.f)
        {
            s = clamp(-d / a, 0.f, 1.f);
            t = 0.f;
        }
        else
        {
            float invDet = 1.f / det;
            s *= invDet;
            t *= invDet;
        }
    }
    else
    {
        if (s < 0.f)
        {
            float tmp0 = b + d;
            float tmp1 = c + e;
            if (tmp1 > tmp0)
            {
                float numer = tmp1 - tmp0;
                float denom = a - 2 * b + c;
                s = clamp(numer / denom, 0.f, 1.f);
                t = 1 - s;
            }
            else
            {
                t = clamp(-e / c, 0.f, 1.f);
                s = 0.f;
            }
        }
        else if (t < 0.f)
        {
            if (a + d > b + e)
            {
                float numer = c + e - b - d;
                float denom = a - 2 * b + c;
                s = clamp(numer / denom, 0.f, 1.f);
                t = 1 - s;
            }
            else
            {
                s = clamp(-e / c, 0.f, 1.f);
                t = 0.f;
            }
        }
        else
        {
            float numer = c + e - b - d;
            float denom = a - 2 * b + c;
            s = clamp(numer / denom, 0.f, 1.f);
            t = 1.f - s;
        }
    }

    return triangle[0] + s * edge0 + t * edge1;
}

#define EPSILON 0.000001
#define CROSS(dest,v1,v2) \
          dest[0]=v1[1]*v2[2]-v1[2]*v2[1]; \
          dest[1]=v1[2]*v2[0]-v1[0]*v2[2]; \
          dest[2]=v1[0]*v2[1]-v1[1]*v2[0];
#define DOT(v1,v2) (v1[0]*v2[0]+v1[1]*v2[1]+v1[2]*v2[2])
#define SUB(dest,v1,v2) \
          dest[0]=v1[0]-v2[0]; \
          dest[1]=v1[1]-v2[1]; \
          dest[2]=v1[2]-v2[2]; 

/* the original jgt code */
int intersect_triangle(double orig[3], double dir[3],
    double vert0[3], double vert1[3], double vert2[3],
    double *t, double *u, double *v)
{
    double edge1[3], edge2[3], tvec[3], pvec[3], qvec[3];
    double det, inv_det;

    /* find vectors for two edges sharing vert0 */
    SUB(edge1, vert1, vert0);
    SUB(edge2, vert2, vert0);

    /* begin calculating determinant - also used to calculate U parameter */
    CROSS(pvec, dir, edge2);

    /* if determinant is near zero, ray lies in plane of triangle */
    det = DOT(edge1, pvec);

    if (det > -EPSILON && det < EPSILON)
        return 0;
    inv_det = 1.0 / det;

    /* calculate distance from vert0 to ray origin */
    SUB(tvec, orig, vert0);

    /* calculate U parameter and test bounds */
    *u = DOT(tvec, pvec) * inv_det;
    if (*u < 0.0 || *u > 1.0)
        return 0;

    /* prepare to test V parameter */
    CROSS(qvec, tvec, edge1);

    /* calculate V parameter and test bounds */
    *v = DOT(dir, qvec) * inv_det;
    if (*v < 0.0 || *u + *v > 1.0)
        return 0;

    /* calculate t, ray intersects triangle */
    *t = DOT(edge2, qvec) * inv_det;

    return 1;
}