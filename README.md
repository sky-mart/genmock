Genmock allows to generate google mocks.

# Usage

## Interface
For example we need a mock for such an interface `hello.h`:
```c++
#ifndef HELLO_H
#define HELLO_H

#include <hieverybody.h>

namespace abc {
namespace xyz {

class IHello
{
public:
    virtual void sayHi() = 0;
};

} // xyz
} // abc

#endif // HELLO_H
```

We run the util (absolute output paths are required, because genmock creates intermediate directories):
```bash
genmock --mocktype=interface --outh=$(pwd)/test.h /path/to/hello.h
```
With the output:
```c++
#ifndef HELLO_MOCK_H
#define HELLO_MOCK_H

#include "hello.h"
#include <gmock/gmock.h>

namespace abc {
namespace xyz {

class HelloMock : public IHello
{
public:
    MOCK_METHOD(void, sayHi, (), (override));
}; // class HelloMock

} // namespace xyz
} // namespace abc
#endif // HELLO_MOCK_H
```
## C API
There are more complex cases of C API, when we want to mock it via a singleton `capi.h`:
```c++
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t operation_a(const uint32_t* px);

void hi_again(const char* name, uint16_t times);

#ifdef __cplusplus
}
#endif
```

After:
```bash
bin/genmock --mocktype=singleton --outh=$(pwd)/CapiMock.h --outsrc=$(pwd)/CapiMock.cpp capi.h
```
Outputs are the following `CapiMock.h`:
```c++
#ifndef CAPI_MOCK_H
#define CAPI_MOCK_H

#include "capi.h"
#include <estd/singleton.h>
#include <gmock/gmock.h>
class CapiMock : public ::esrlabs::estd::singleton<CapiMock>
{
public:

    MOCK_METHOD(int, operation_a, (const int *));
    MOCK_METHOD(void, hi_again, (const char *, int));
}; // class CapiMock

#endif // CAPI_MOCK_H
```
`CapiMock.cpp`
```c++
#include "capi.h"
#include "test.h"

extern "C" {

int operation_a(const int *px)
{
    return CapiMock::instance().operation_a(px);
}

void hi_again(const char *name, int times)
{
    CapiMock::instance().hi_again(name, times);
}

} // extern "C"
```

## Static methods API
One case also requires a singleton to implement a mock:
```c++
#ifndef STATISTA_H
#define STATISTA_H

#include <hieverybody.h>
#include <stdint.h>

namespace abc {
namespace xyz {

class Statista
{
public:
    static void calculate(uint32_t data);

    static float median();
};

} // xyz
} // abc

#endif // STATISTA_H
```

We run genmock
```bash
bin/genmock --mocktype=singleton --outh=$(pwd)/StatistaMock.h --outsrc=$(pwd)/Statista.cpp Statista.h
```
With the following results `StatistaMock.h`:
```c++
#ifndef STATISTA_MOCK_H
#define STATISTA_MOCK_H

#include "Statista.h"
#include <estd/singleton.h>
#include <gmock/gmock.h>

namespace abc {
namespace xyz {

class StatistaMock : public ::esrlabs::estd::singleton<StatistaMock>
{
public:
    StatistaMock() : ::esrlabs::estd::singleton<StatistaMock>(*this) {}

    MOCK_METHOD1(calculate, void(int));
    MOCK_METHOD0(median, float());
}; // class StatistaMock

} // namespace xyz
} // namespace abc
#endif // STATISTA_MOCK_H
```
And `StatistaMock.cpp`:
```c++
#include "Statista.h"
#include "test.h"

namespace abc {
namespace xyz {

void Statista::calculate(int data)
{
    StatistaMock::instance().calculate(data);
}

float Statista::median()
{
    return StatistaMock::instance().median();
}

} // namespace xyz
} // namespace abc
```

# Configuration
Genmock supports configuration. Either via --config argument or via the default path ~/.config/genmock/genmock.json

Configuration is a json file with 4 parameters:
```json
{
	"tab_length": 4,
	"singleton_path": "<estd/singleton.h>",
	"singleton_class": "::esrlabs::estd::singleton",
	"style": "old"
}
```

The style parameter defines the style of google mock. See http://google.github.io/googletest/gmock_cook_book.html#old-style-mock_methodn-macros

# Build

Build requires cmake, ninja and nlohmann_json on the system level.

```bash
git clone --depth=1 https://github.com/llvm/llvm-project.git
cd llvm-project
git submodule add https://github.com/sky-mart/genmock.git clang-tools-extra/genmock
# add_subdirectory in clang-tools-extra/CMakeLists.txt
mkdir build
cd build
cmake -G Ninja ../llvm -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON .
ninja genmock -j16
cp bin/genmock ~/.local/bin/ # or other directory
```
