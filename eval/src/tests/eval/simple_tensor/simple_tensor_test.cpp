// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
#include <vespa/vespalib/testkit/test_kit.h>
#include <vespa/eval/eval/simple_tensor.h>
#include <vespa/eval/eval/simple_tensor_engine.h>
#include <vespa/eval/eval/operation.h>
#include <vespa/vespalib/util/stash.h>
#include <vespa/vespalib/data/memory.h>
#include <vespa/vespalib/objects/nbostream.h>
#include <iostream>

using namespace vespalib::eval;

using Cell = SimpleTensor::Cell;
using Cells = SimpleTensor::Cells;
using Address = SimpleTensor::Address;
using Stash = vespalib::Stash;
using vespalib::nbostream;
using vespalib::Memory;

TensorSpec to_spec(const Value &a) { return SimpleTensorEngine::ref().to_spec(a); }

const Tensor &unwrap(const Value &value) {
    ASSERT_TRUE(value.is_tensor());
    return *value.as_tensor();
}

struct CellBuilder {
    Cells cells;
    CellBuilder &add(const Address &addr, double value) {
        cells.emplace_back(addr, value);
        return *this;
    }
    Cells build() { return cells; }
};

TEST("require that simple tensors can be built using tensor spec") {
    TensorSpec spec("tensor(w{},x[2],y{},z[2])");
    spec.add({{"w", "xxx"}, {"x", 0}, {"y", "xxx"}, {"z", 0}}, 1.0)
        .add({{"w", "xxx"}, {"x", 0}, {"y", "yyy"}, {"z", 1}}, 2.0)
        .add({{"w", "yyy"}, {"x", 1}, {"y", "xxx"}, {"z", 0}}, 3.0)
        .add({{"w", "yyy"}, {"x", 1}, {"y", "yyy"}, {"z", 1}}, 4.0);
    Value::UP tensor = SimpleTensorEngine::ref().from_spec(spec);
    TensorSpec full_spec("tensor(w{},x[2],y{},z[2])");
    full_spec
        .add({{"w", "xxx"}, {"x", 0}, {"y", "xxx"}, {"z", 0}}, 1.0)
        .add({{"w", "xxx"}, {"x", 0}, {"y", "xxx"}, {"z", 1}}, 0.0)
        .add({{"w", "xxx"}, {"x", 0}, {"y", "yyy"}, {"z", 0}}, 0.0)
        .add({{"w", "xxx"}, {"x", 0}, {"y", "yyy"}, {"z", 1}}, 2.0)
        .add({{"w", "xxx"}, {"x", 1}, {"y", "xxx"}, {"z", 0}}, 0.0)
        .add({{"w", "xxx"}, {"x", 1}, {"y", "xxx"}, {"z", 1}}, 0.0)
        .add({{"w", "xxx"}, {"x", 1}, {"y", "yyy"}, {"z", 0}}, 0.0)
        .add({{"w", "xxx"}, {"x", 1}, {"y", "yyy"}, {"z", 1}}, 0.0)
        .add({{"w", "yyy"}, {"x", 0}, {"y", "xxx"}, {"z", 0}}, 0.0)
        .add({{"w", "yyy"}, {"x", 0}, {"y", "xxx"}, {"z", 1}}, 0.0)
        .add({{"w", "yyy"}, {"x", 0}, {"y", "yyy"}, {"z", 0}}, 0.0)
        .add({{"w", "yyy"}, {"x", 0}, {"y", "yyy"}, {"z", 1}}, 0.0)
        .add({{"w", "yyy"}, {"x", 1}, {"y", "xxx"}, {"z", 0}}, 3.0)
        .add({{"w", "yyy"}, {"x", 1}, {"y", "xxx"}, {"z", 1}}, 0.0)
        .add({{"w", "yyy"}, {"x", 1}, {"y", "yyy"}, {"z", 0}}, 0.0)
        .add({{"w", "yyy"}, {"x", 1}, {"y", "yyy"}, {"z", 1}}, 4.0);
    Value::UP full_tensor = SimpleTensorEngine::ref().from_spec(full_spec);
    EXPECT_EQUAL(full_spec, to_spec(*tensor));
    EXPECT_EQUAL(full_spec, to_spec(*full_tensor));
};

TEST("require that simple tensors can have their values negated") {
    auto tensor = SimpleTensor::create(
            TensorSpec("tensor(x{},y{})")
            .add({{"x","1"},{"y","1"}}, 1)
            .add({{"x","2"},{"y","1"}}, -3)
            .add({{"x","1"},{"y","2"}}, 5));
    auto expect = SimpleTensor::create(
            TensorSpec("tensor(x{},y{})")
            .add({{"x","1"},{"y","1"}}, -1)
            .add({{"x","2"},{"y","1"}}, 3)
            .add({{"x","1"},{"y","2"}}, -5));
    auto result = tensor->map([](double a){ return -a; });
    EXPECT_EQUAL(to_spec(*expect), to_spec(*result));
    Stash stash;
    const Value &result2 = SimpleTensorEngine::ref().map(*tensor, operation::Neg::f, stash);
    EXPECT_EQUAL(to_spec(*expect), to_spec(unwrap(result2)));    
}

TEST("require that simple tensors can be multiplied with each other") {
    auto lhs = SimpleTensor::create(
            TensorSpec("tensor(x{},y{})")
            .add({{"x","1"},{"y","1"}}, 1)
            .add({{"x","2"},{"y","1"}}, 3)
            .add({{"x","1"},{"y","2"}}, 5));
    auto rhs = SimpleTensor::create(
            TensorSpec("tensor(y{},z{})")
            .add({{"y","1"},{"z","1"}}, 7)
            .add({{"y","2"},{"z","1"}}, 11)
            .add({{"y","1"},{"z","2"}}, 13));
    auto expect = SimpleTensor::create(
            TensorSpec("tensor(x{},y{},z{})")
            .add({{"x","1"},{"y","1"},{"z","1"}}, 7)
            .add({{"x","1"},{"y","1"},{"z","2"}}, 13)
            .add({{"x","2"},{"y","1"},{"z","1"}}, 21)
            .add({{"x","2"},{"y","1"},{"z","2"}}, 39)
            .add({{"x","1"},{"y","2"},{"z","1"}}, 55));
    auto result = SimpleTensor::join(*lhs, *rhs, [](double a, double b){ return (a * b); });
    EXPECT_EQUAL(to_spec(*expect), to_spec(*result));
    Stash stash;
    const Value &result2 = SimpleTensorEngine::ref().join(*lhs, *rhs, operation::Mul::f, stash);
    EXPECT_EQUAL(to_spec(*expect), to_spec(unwrap(result2)));
}

TEST("require that simple tensors support dimension reduction") {
    auto tensor = SimpleTensor::create(
            TensorSpec("tensor(x[3],y[2])")
            .add({{"x",0},{"y",0}}, 1)
            .add({{"x",1},{"y",0}}, 2)
            .add({{"x",2},{"y",0}}, 3)
            .add({{"x",0},{"y",1}}, 4)
            .add({{"x",1},{"y",1}}, 5)
            .add({{"x",2},{"y",1}}, 6));
    auto expect_sum_y = SimpleTensor::create(
            TensorSpec("tensor(x[3])")
            .add({{"x",0}}, 5)
            .add({{"x",1}}, 7)
            .add({{"x",2}}, 9));
    auto expect_sum_x = SimpleTensor::create(
            TensorSpec("tensor(y[2])")
            .add({{"y",0}}, 6)
            .add({{"y",1}}, 15));
    auto expect_sum_all = SimpleTensor::create(TensorSpec("double").add({}, 21));
    Stash stash;
    Aggregator &aggr_sum = Aggregator::create(Aggr::SUM, stash);
    auto result_sum_y = tensor->reduce(aggr_sum, {"y"});
    auto result_sum_x = tensor->reduce(aggr_sum, {"x"});
    auto result_sum_all = tensor->reduce(aggr_sum, {"x", "y"});
    EXPECT_EQUAL(to_spec(*expect_sum_y), to_spec(*result_sum_y));
    EXPECT_EQUAL(to_spec(*expect_sum_x), to_spec(*result_sum_x));
    EXPECT_EQUAL(to_spec(*expect_sum_all), to_spec(*result_sum_all));
    const Value &result_sum_y_2 = SimpleTensorEngine::ref().reduce(*tensor, Aggr::SUM, {"y"}, stash);
    const Value &result_sum_x_2 = SimpleTensorEngine::ref().reduce(*tensor, Aggr::SUM, {"x"}, stash);
    const Value &result_sum_all_2 = SimpleTensorEngine::ref().reduce(*tensor, Aggr::SUM, {"x", "y"}, stash); 
    const Value &result_sum_all_3 = SimpleTensorEngine::ref().reduce(*tensor, Aggr::SUM, {}, stash);
    EXPECT_EQUAL(to_spec(*expect_sum_y), to_spec(unwrap(result_sum_y_2)));
    EXPECT_EQUAL(to_spec(*expect_sum_x), to_spec(unwrap(result_sum_x_2)));
    EXPECT_TRUE(result_sum_all_2.is_double());
    EXPECT_TRUE(result_sum_all_3.is_double());
    EXPECT_EQUAL(21, result_sum_all_2.as_double());
    EXPECT_EQUAL(21, result_sum_all_3.as_double());
    EXPECT_EQUAL(to_spec(*result_sum_y), to_spec(*result_sum_y));
    EXPECT_NOT_EQUAL(to_spec(*result_sum_y), to_spec(*result_sum_x));
}

//-----------------------------------------------------------------------------

struct SparseTensorExample {
    TensorSpec make_spec() const {
        return TensorSpec("tensor(x{},y{})")
            .add({{"x","a"},{"y","a"}}, 1)
            .add({{"x","a"},{"y","b"}}, 2)
            .add({{"x","b"},{"y","a"}}, 3);
    }
    std::unique_ptr<SimpleTensor> make_tensor() const {
        return SimpleTensor::create(make_spec());
    }
    template <typename T>
    void encode_inner(nbostream &dst) const {
        dst.putInt1_4Bytes(2);
        dst.writeSmallString("x");
        dst.writeSmallString("y");
        dst.putInt1_4Bytes(3);
        dst.writeSmallString("a");
        dst.writeSmallString("a");
        dst << (T) 1;
        dst.writeSmallString("a");
        dst.writeSmallString("b");
        dst << (T) 2;
        dst.writeSmallString("b");
        dst.writeSmallString("a");
        dst << (T) 3;
    }
    void encode_default(nbostream &dst) const {
        dst.putInt1_4Bytes(1);
        encode_inner<double>(dst);
    }
    void encode_with_double(nbostream &dst) const {
        dst.putInt1_4Bytes(5);
        dst.putInt1_4Bytes(0);
        encode_inner<double>(dst);
    }
    void encode_with_float(nbostream &dst) const {
        dst.putInt1_4Bytes(5);
        dst.putInt1_4Bytes(1);
        encode_inner<float>(dst);
    }
};

TEST_F("require that sparse tensors can be decoded", SparseTensorExample()) {
    nbostream data1;
    nbostream data2;
    nbostream data3;
    f1.encode_default(data1);
    f1.encode_with_double(data2);
    f1.encode_with_float(data3);
    EXPECT_EQUAL(to_spec(*SimpleTensor::decode(data1)), f1.make_spec());
    EXPECT_EQUAL(to_spec(*SimpleTensor::decode(data2)), f1.make_spec());
    EXPECT_EQUAL(to_spec(*SimpleTensor::decode(data3)), f1.make_spec());
}

TEST_F("require that sparse tensors can be encoded", SparseTensorExample()) {
    nbostream data;
    nbostream expect;
    SimpleTensor::encode(*f1.make_tensor(), data);
    f1.encode_default(expect);
    EXPECT_EQUAL(Memory(data.peek(), data.size()), Memory(expect.peek(), expect.size()));
}

//-----------------------------------------------------------------------------

struct DenseTensorExample {
    TensorSpec make_spec() const {
        return TensorSpec("tensor(x[3],y[2])")
            .add({{"x",0},{"y",0}}, 1)
            .add({{"x",0},{"y",1}}, 2)
            .add({{"x",1},{"y",0}}, 3)
            .add({{"x",1},{"y",1}}, 4)
            .add({{"x",2},{"y",0}}, 5)
            .add({{"x",2},{"y",1}}, 6);
    }
    std::unique_ptr<SimpleTensor> make_tensor() const {
        return SimpleTensor::create(make_spec());
    }
    template <typename T>
    void encode_inner(nbostream &dst) const {
        dst.putInt1_4Bytes(2);
        dst.writeSmallString("x");
        dst.putInt1_4Bytes(3);
        dst.writeSmallString("y");
        dst.putInt1_4Bytes(2);
        dst << (T) 1;
        dst << (T) 2;
        dst << (T) 3;
        dst << (T) 4;
        dst << (T) 5;
        dst << (T) 6;
    }
    void encode_default(nbostream &dst) const {
        dst.putInt1_4Bytes(2);
        encode_inner<double>(dst);
    }
    void encode_with_double(nbostream &dst) const {
        dst.putInt1_4Bytes(6);
        dst.putInt1_4Bytes(0);
        encode_inner<double>(dst);
    }
    void encode_with_float(nbostream &dst) const {
        dst.putInt1_4Bytes(6);
        dst.putInt1_4Bytes(1);
        encode_inner<float>(dst);
    }
};

TEST_F("require that dense tensors can be decoded", DenseTensorExample()) {
    nbostream data1;
    nbostream data2;
    nbostream data3;
    f1.encode_default(data1);
    f1.encode_with_double(data2);
    f1.encode_with_float(data3);
    EXPECT_EQUAL(to_spec(*SimpleTensor::decode(data1)), f1.make_spec());
    EXPECT_EQUAL(to_spec(*SimpleTensor::decode(data2)), f1.make_spec());
    EXPECT_EQUAL(to_spec(*SimpleTensor::decode(data3)), f1.make_spec());
}

TEST_F("require that dense tensors can be encoded", DenseTensorExample()) {
    nbostream data;
    nbostream expect;
    SimpleTensor::encode(*f1.make_tensor(), data);
    f1.encode_default(expect);
    EXPECT_EQUAL(Memory(data.peek(), data.size()), Memory(expect.peek(), expect.size()));
}

//-----------------------------------------------------------------------------

struct MixedTensorExample {
    TensorSpec make_spec() const {
        return TensorSpec("tensor(x{},y{},z[2])")
            .add({{"x","a"},{"y","a"},{"z",0}}, 1)
            .add({{"x","a"},{"y","a"},{"z",1}}, 2)
            .add({{"x","a"},{"y","b"},{"z",0}}, 3)
            .add({{"x","a"},{"y","b"},{"z",1}}, 4)
            .add({{"x","b"},{"y","a"},{"z",0}}, 5)
            .add({{"x","b"},{"y","a"},{"z",1}}, 6);
    }
    std::unique_ptr<SimpleTensor> make_tensor() const {
        return SimpleTensor::create(make_spec());
    }
    template <typename T>
    void encode_inner(nbostream &dst) const {
        dst.putInt1_4Bytes(2);
        dst.writeSmallString("x");
        dst.writeSmallString("y");
        dst.putInt1_4Bytes(1);
        dst.writeSmallString("z");
        dst.putInt1_4Bytes(2);
        dst.putInt1_4Bytes(3);
        dst.writeSmallString("a");
        dst.writeSmallString("a");
        dst << (T) 1;
        dst << (T) 2;
        dst.writeSmallString("a");
        dst.writeSmallString("b");
        dst << (T) 3;
        dst << (T) 4;
        dst.writeSmallString("b");
        dst.writeSmallString("a");
        dst << (T) 5;
        dst << (T) 6;
    }
    void encode_default(nbostream &dst) const {
        dst.putInt1_4Bytes(3);
        encode_inner<double>(dst);
    }
    void encode_with_double(nbostream &dst) const {
        dst.putInt1_4Bytes(7);
        dst.putInt1_4Bytes(0);
        encode_inner<double>(dst);
    }
    void encode_with_float(nbostream &dst) const {
        dst.putInt1_4Bytes(7);
        dst.putInt1_4Bytes(1);
        encode_inner<float>(dst);
    }
};

TEST_F("require that mixed tensors can be decoded", MixedTensorExample()) {
    nbostream data1;
    nbostream data2;
    nbostream data3;
    f1.encode_default(data1);
    f1.encode_with_double(data2);
    f1.encode_with_float(data3);
    EXPECT_EQUAL(to_spec(*SimpleTensor::decode(data1)), f1.make_spec());
    EXPECT_EQUAL(to_spec(*SimpleTensor::decode(data2)), f1.make_spec());
    EXPECT_EQUAL(to_spec(*SimpleTensor::decode(data3)), f1.make_spec());
}

TEST_F("require that mixed tensors can be encoded", MixedTensorExample()) {
    nbostream data;
    nbostream expect;
    SimpleTensor::encode(*f1.make_tensor(), data);
    f1.encode_default(expect);
    EXPECT_EQUAL(Memory(data.peek(), data.size()), Memory(expect.peek(), expect.size()));
}

//-----------------------------------------------------------------------------

TEST_MAIN() { TEST_RUN_ALL(); }
