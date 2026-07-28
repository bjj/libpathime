/*
 * Where this library is.
 *
 * pathime_init_params_t::resource_dir defaults to a directory beside the
 * libpathime binary, so that a client which ships the data next to the library
 * needs to configure nothing. Answering "beside which file, exactly" is the
 * whole of this header's job, and it is the only place in the library that
 * asks the platform about itself.
 */

#ifndef LIBPATHIME_SRC_MODULE_PATH_H
#define LIBPATHIME_SRC_MODULE_PATH_H

#include <string>

namespace pathime {

/**
 * The absolute directory containing the binary this code is linked into: the
 * libpathime shared library, or the program itself in a static build. No
 * trailing separator.
 *
 * Empty if the platform declined to say, which leaves every engine that needs
 * a data file reporting itself unavailable through pathime_has_engine(). The
 * result is UTF-8 and unmangled for any path the platform accepts.
 */
std::string module_dir();

}  // namespace pathime

#endif /* LIBPATHIME_SRC_MODULE_PATH_H */
