#ifndef DISTORTED_GRID_LOADER_H_
#define DISTORTED_GRID_LOADER_H_

#include <vector>
#include <string>
#include "glm/glm.hpp"

namespace gvr {

class DistortedGridLoader {
public:
    DistortedGridLoader() {
    }
    ;
    ~DistortedGridLoader() {
    }
    ;

    static void loadVertices(std::vector<glm::vec3>& vertices, int index);

private:
    static void loadString(std::string& str);
};

}
#endif
