#include "TestManyChildAllocs.h"

#include "IPDLUnitTests.h"      // fail etc.


#define NALLOCS 10

namespace mozilla {
namespace _ipdltest {

// parent code

TestManyChildAllocsParent::TestManyChildAllocsParent()
{
    MOZ_COUNT_CTOR(TestManyChildAllocsParent);
}

TestManyChildAllocsParent::~TestManyChildAllocsParent()
{
    MOZ_COUNT_DTOR(TestManyChildAllocsParent);
}

void
TestManyChildAllocsParent::Main()
{
    if (!SendGo())
        fail("can't send Go()");
}

bool
TestManyChildAllocsParent::RecvDone()
{
    // explicitly *not* cleaning up, so we can sanity-check IPDL's
    // auto-shutdown/cleanup handling
    Close();

    return true;
}

bool
TestManyChildAllocsParent::DeallocPTestManyChildAllocsSub(
    PTestManyChildAllocsSubParent* __a)
{
    delete __a; return true;
}

PTestManyChildAllocsSubParent*
TestManyChildAllocsParent::AllocPTestManyChildAllocsSub()
{
    return new TestManyChildAllocsSubParent();
}


// child code

TestManyChildAllocsChild::TestManyChildAllocsChild()
{
    MOZ_COUNT_CTOR(TestManyChildAllocsChild);
}

TestManyChildAllocsChild::~TestManyChildAllocsChild()
{
    MOZ_COUNT_DTOR(TestManyChildAllocsChild);
}

bool TestManyChildAllocsChild::RecvGo()
{
    for (int i = 0; i < NALLOCS; ++i) {
        PTestManyChildAllocsSubChild* child =
            SendPTestManyChildAllocsSubConstructor();

        if (!child)
            fail("can't send ctor()");

        if (!child->SendHello())
            fail("can't send Hello()");
    }

    nsTArray<PTestManyChildAllocsSubChild*> kids;
    ManagedPTestManyChildAllocsSubChild(kids);

    if (NALLOCS != kids.Length())
        fail("expected %lu kids, got %lu", NALLOCS, kids.Length());

    if (!SendDone())
        fail("can't send Done()");

    return true;
}

bool
TestManyChildAllocsChild::DeallocPTestManyChildAllocsSub(
    PTestManyChildAllocsSubChild* __a)
{
    delete __a; return true;
}

PTestManyChildAllocsSubChild*
TestManyChildAllocsChild::AllocPTestManyChildAllocsSub()
{
    return new TestManyChildAllocsSubChild();
}


} // namespace _ipdltest
} // namespace mozilla
