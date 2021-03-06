// Copyright 2019 Oath Inc. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include <vespa/eval/eval/fast_value.h>
#include <vespa/eval/eval/tensor_function.h>
#include <vespa/eval/eval/test/eval_fixture.h>
#include <vespa/eval/eval/test/tensor_model.hpp>
#include <vespa/eval/instruction/dense_tensor_peek_function.h>
#include <vespa/eval/tensor/default_tensor_engine.h>
#include <vespa/vespalib/testkit/test_kit.h>
#include <vespa/vespalib/util/stash.h>
#include <vespa/vespalib/util/stringfmt.h>

using namespace vespalib;
using namespace vespalib::eval;
using namespace vespalib::eval::test;
using namespace vespalib::eval::tensor_function;

const TensorEngine &old_engine = tensor::DefaultTensorEngine::ref();
const ValueBuilderFactory &prod_factory = FastValueBuilderFactory::get();

EvalFixture::ParamRepo make_params() {
    return EvalFixture::ParamRepo()
        .add("a", spec(1.0))
        .add("b", spec(2.0))
        .add("c", spec(3.0))
        .add("x3", spec(x(3), N()))
        .add("x3f", spec(float_cells({x(3)}), N()))
        .add("x3y2", spec({x(3),y(2)}, N()))
        .add("x3y2f", spec(float_cells({x(3),y(2)}), N()))
        .add("xm", spec(x({"1","2","3","-1","-2","-3"}), N()))
        .add("xmy2", spec({x({"1","2","3"}), y(2)}, N()));
}
EvalFixture::ParamRepo param_repo = make_params();

void verify(const vespalib::string &expr, double expect, size_t expect_optimized_cnt, size_t expect_not_optimized_cnt) {
    EvalFixture fixture(prod_factory, expr, param_repo, true);
    auto expect_spec = TensorSpec("double").add({}, expect);
    EXPECT_EQUAL(EvalFixture::ref(expr, param_repo), expect_spec);
    EXPECT_EQUAL(fixture.result(), expect_spec);
    auto info = fixture.find_all<DenseTensorPeekFunction>();
    EXPECT_EQUAL(info.size(), expect_optimized_cnt);
    for (size_t i = 0; i < info.size(); ++i) {
        EXPECT_TRUE(info[i]->result_is_mutable());
    }
    EXPECT_EQUAL(fixture.find_all<Peek>().size(), expect_not_optimized_cnt);

    EvalFixture old_fixture(old_engine, expr, param_repo, true);
    EXPECT_EQUAL(old_fixture.result(), expect_spec);
    info = old_fixture.find_all<DenseTensorPeekFunction>();
    EXPECT_EQUAL(info.size(), expect_optimized_cnt);
    for (size_t i = 0; i < info.size(); ++i) {
        EXPECT_TRUE(info[i]->result_is_mutable());
    }
    EXPECT_EQUAL(old_fixture.find_all<Peek>().size(), expect_not_optimized_cnt);
}

//-----------------------------------------------------------------------------

TEST("require that tensor peek can be optimized for dense tensors") {
    TEST_DO(verify("x3{x:0}", 1.0, 1, 0));
    TEST_DO(verify("x3{x:(a)}", 2.0, 1, 0));
    TEST_DO(verify("x3f{x:(c-1)}", 3.0, 1, 0));
    TEST_DO(verify("x3{x:(c+5)}", 0.0, 1, 0));
    TEST_DO(verify("x3{x:(a-2)}", 0.0, 1, 0));
    TEST_DO(verify("x3y2{x:(a),y:(a-1)}", 3.0, 1, 0));
    TEST_DO(verify("x3y2f{x:1,y:(a)}", 4.0, 1, 0));
    TEST_DO(verify("x3y2f{x:(a-1),y:(b)}", 0.0, 1, 0));
}

TEST("require that tensor peek is not optimized for sparse tensor") {
    TEST_DO(verify("xm{x:1}", 1.0, 0, 1));
    TEST_DO(verify("xm{x:(c)}", 3.0, 0, 1));
    TEST_DO(verify("xm{x:(c+1)}", 0.0, 0, 1));
}

TEST("require that tensor peek is not optimized for mixed tensor") {
    TEST_DO(verify("xmy2{x:3,y:1}", 6.0, 0, 1));
    TEST_DO(verify("xmy2{x:(c),y:(a)}", 6.0, 0, 1));
    TEST_DO(verify("xmy2{x:(a),y:(b)}", 0.0, 0, 1));
}

TEST("require that indexes are truncated when converted to integers") {
    TEST_DO(verify("x3{x:(a+0.7)}", 2.0, 1, 0));
    TEST_DO(verify("x3{x:(a+0.3)}", 2.0, 1, 0));
    TEST_DO(verify("xm{x:(a+0.7)}", 1.0, 0, 1));
    TEST_DO(verify("xm{x:(a+0.3)}", 1.0, 0, 1));
    TEST_DO(verify("xm{x:(-a-0.7)}", 4.0, 0, 1));
    TEST_DO(verify("xm{x:(-a-0.3)}", 4.0, 0, 1));
}

TEST_MAIN() { TEST_RUN_ALL(); }
