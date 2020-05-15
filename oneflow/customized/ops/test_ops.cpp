#include "oneflow/core/framework/framework.h"
#include "oneflow/core/common/balanced_splitter.h"

namespace oneflow {

namespace {

void PrintSbpLog(SbpSignatureList* sbp_list) {
  for (const auto& sbp_sign : sbp_list->sbp_signature()) {
    std::cout << "cclog: one sbp sign: ";
    for (const auto& pair : sbp_sign.bn_in_op2sbp_parallel()) {
      std::cout << " bn: " << pair.first;
      if (pair.second.has_split_parallel()) {
        std::cout << " Split axis = " << pair.second.split_parallel().axis();
      } else if (pair.second.has_broadcast_parallel()) {
        std::cout << " Broadcast ";
      } else if (pair.second.has_partial_sum_parallel()) {
        std::cout << " PartialSum ";
      } else {
        std::cout << " ERROR !";
      }
    }
    std::cout << std::endl;
  }
}

}  // namespace

REGISTER_USER_OP("ccrelu")
    .Input("in")
    .Output("out")
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      Shape* in_shape = ctx->Shape4ArgNameAndIndex("in", 0);
      Shape* out_shape = ctx->Shape4ArgNameAndIndex("out", 0);
      *out_shape = *in_shape;
      // int32_t last_axis = in_shape->NumAxes() - 1;
      // out_shape->Set(last_axis, in_shape->At(last_axis) * 2);
      return Maybe<void>::Ok();
    })
    .SetGetSbpFn([](user_op::SbpContext* ctx) -> Maybe<void> {
      ctx->NewBuilder().Split(ctx->inputs(), 0).Split(ctx->outputs(), 0).Build();
      return Maybe<void>::Ok();
    });

REGISTER_USER_OP("ccrelu_grad")
    .Input("y")
    .Input("dy")
    .Output("dx")
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      Shape* y_shape = ctx->Shape4ArgNameAndIndex("y", 0);
      Shape* dy_shape = ctx->Shape4ArgNameAndIndex("dy", 0);
      Shape* dx_shape = ctx->Shape4ArgNameAndIndex("dx", 0);
      CHECK(*dy_shape == *y_shape);
      *dx_shape = *y_shape;
      // int32_t last_axis = y_shape->NumAxes() - 1;
      // dx_shape->Set(last_axis, y_shape->At(last_axis) / 2);
      return Maybe<void>::Ok();
    })
    .SetGetSbpFn([](user_op::SbpContext* ctx) -> Maybe<void> {
      ctx->NewBuilder()
          .Split(user_op::OpArg("y", 0), 0)
          .Split(user_op::OpArg("dy", 0), 0)
          .Split(user_op::OpArg("dx", 0), 0)
          .Build();
      return Maybe<void>::Ok();
    });

REGISTER_USER_OP_GRAD("ccrelu").SetGenBackwardOpConfFn([](const user_op::UserOpWrapper& op,
                                                          user_op::AddOpFn AddOp) {
  if (op.NeedGenGradTensor4OpInput("in", 0)) {
    user_op::UserOpConfWrapperBuilder builder(op.op_name() + "_grad");
    user_op::UserOpConfWrapper ccrelu_grad_op =
        builder.Op("ccrelu_grad")
            .Input("y", op.output("out", 0))
            .Input("dy", op.GetGradTensorWithOpOutput("out", 0))
            .Output("dx")
            .Build();
    op.BindGradTensorWithOpInput(ccrelu_grad_op.output("dx", 0), "in", 0);
    AddOp(ccrelu_grad_op);
  }
});

REGISTER_USER_OP("TestReshape")
    .Input("in")
    .Output("out")
    .Attr("shape", UserOpAttrType::kAtShape)
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      const Shape* in_shape = ctx->Shape4ArgNameAndIndex("in", 0);
      Shape* out_shape = ctx->Shape4ArgNameAndIndex("out", 0);
      const Shape& conf_shape = ctx->GetAttr<Shape>("shape");
      CHECK_EQ(in_shape->NumAxes(), conf_shape.NumAxes());
      *out_shape = conf_shape;
      return Maybe<void>::Ok();
    });

REGISTER_USER_OP("TestReshape4KeepHeaderOnly")
    .Input("in")
    .Output("out")
    .Attr("shape", UserOpAttrType::kAtShape)
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      const Shape* in_shape = ctx->Shape4ArgNameAndIndex("in", 0);
      Shape* out_shape = ctx->Shape4ArgNameAndIndex("out", 0);
      const Shape& conf_shape = ctx->GetAttr<Shape>("shape");
      CHECK_EQ(in_shape->elem_cnt(), conf_shape.elem_cnt());
      *out_shape = conf_shape;
      return Maybe<void>::Ok();
    });

REGISTER_USER_OP("TestReshapeLike4KeepHeaderOnly")
    .Input("in")
    .Input("like")
    .Output("out")
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      const Shape* in_shape = ctx->Shape4ArgNameAndIndex("in", 0);
      const Shape* like_shape = ctx->Shape4ArgNameAndIndex("like", 0);
      Shape* out_shape = ctx->Shape4ArgNameAndIndex("out", 0);
      CHECK_EQ(in_shape->elem_cnt(), like_shape->elem_cnt());
      *out_shape = *like_shape;
      return Maybe<void>::Ok();
    })
    .SetInputArgModifyFn([](user_op::GetInputArgModifier GetInputArgModifierFn,
                            const user_op::UserOpConfWrapper& conf) {
      CHECK_EQ(conf.input_size("like"), 1);
      user_op::InputArgModifier* like_arg_modifier = GetInputArgModifierFn("like", 0);
      CHECK(like_arg_modifier != nullptr);
      like_arg_modifier->set_use_header_only(true);
    });

REGISTER_USER_OP_GRAD("TestReshape4KeepHeaderOnly")
    .SetGenBackwardOpConfFn([](const user_op::UserOpWrapper& op, user_op::AddOpFn AddOp) {
      if (op.NeedGenGradTensor4OpInput("in", 0)) {
        user_op::UserOpConfWrapperBuilder builder(op.op_name() + "_grad");
        user_op::UserOpConfWrapper test_reshape_like_op =
            builder.Op("TestReshapeLike4KeepHeaderOnly")
                .Input("in", op.GetGradTensorWithOpOutput("out", 0))
                .Input("like", op.input("in", 0))
                .Output("out")
                .Build();
        op.BindGradTensorWithOpInput(test_reshape_like_op.output("out", 0), "in", 0);
        AddOp(test_reshape_like_op);
      }
    });

REGISTER_USER_OP("TestSource")
    .Output("out")
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      Shape* out_shape = ctx->Shape4ArgNameAndIndex("out", 0);
      *out_shape = Shape({5});
      *ctx->Dtype4ArgNameAndIndex("out", 0) = DataType::kFloat;
      return Maybe<void>::Ok();
    })
    .SetBatchAxisInferFn([](user_op::BatchAxisContext* ctx) -> Maybe<void> {
      ctx->BatchAxis4ArgNameAndIndex("out", 0)->set_value(0);
      return Maybe<void>::Ok();
    })
    .SetGetSbpFn([](user_op::SbpContext* ctx) -> Maybe<void> {
      ctx->NewBuilder().Split(ctx->outputs(), 0).Build();
      return Maybe<void>::Ok();
    });

REGISTER_USER_OP("TestMultiOutputOrder")
    .Input("in")
    .Output("out1")
    .Output("out2")
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      Shape* in_shape = ctx->Shape4ArgNameAndIndex("in", 0);
      Shape* out1_shape = ctx->Shape4ArgNameAndIndex("out1", 0);
      Shape* out2_shape = ctx->Shape4ArgNameAndIndex("out2", 0);
      *out1_shape = *in_shape;
      *out2_shape = *in_shape;
      int32_t last_axis = in_shape->NumAxes() - 1;
      out2_shape->Set(last_axis, in_shape->At(last_axis) * 2);
      return Maybe<void>::Ok();
    })
    .SetGetSbpFn([](user_op::SbpContext* ctx) -> Maybe<void> {
      ctx->NewBuilder().Split(ctx->outputs(), 0).Build();
      return Maybe<void>::Ok();
    });

REGISTER_USER_OP("TestSourceMultiGpuFixedOutNum")
    .Output("out")
    .Attr("out_num", UserOpAttrType::kAtInt64)
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      Shape* out_shape = ctx->Shape4ArgNameAndIndex("out", 0);
      int64_t out_num = ctx->GetAttr<int64_t>("out_num");
      const ParallelContext& parallel_ctx = ctx->parallel_ctx();
      BalancedSplitter bs(out_num, parallel_ctx.parallel_num());
      *out_shape = Shape({bs.At(parallel_ctx.parallel_id()).size()});

      const SbpParallel& out_sbp = ctx->SbpParallel4ArgNameAndIndex("out", 0);
      CHECK(out_sbp.has_split_parallel() && out_sbp.split_parallel().axis() == 0);
      *ctx->Dtype4ArgNameAndIndex("out", 0) = DataType::kFloat;
      return Maybe<void>::Ok();
    })
    .SetBatchAxisInferFn([](user_op::BatchAxisContext* ctx) -> Maybe<void> {
      ctx->BatchAxis4ArgNameAndIndex("out", 0)->set_value(0);
      return Maybe<void>::Ok();
    })
    .SetGetSbpFn([](user_op::SbpContext* ctx) -> Maybe<void> {
      int64_t parallel_num = ctx->parallel_num();
      DeviceType device_type = ctx->device_type();
      if (device_type == DeviceType::kCPU && parallel_num > 1) {
        ctx->NewBuilder().Split(ctx->outputs(), 0).Build();
      }
      return Maybe<void>::Ok();
    });

REGISTER_USER_OP("TestMultiInput")
    .Input("x1")
    .Input("x2")
    .Output("y")
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      Shape* x1_shape = ctx->Shape4ArgNameAndIndex("x1", 0);
      Shape* x2_shape = ctx->Shape4ArgNameAndIndex("x2", 0);
      Shape* y_shape = ctx->Shape4ArgNameAndIndex("y", 0);
      CHECK(*x1_shape == *x2_shape);
      *y_shape = *x1_shape;
      return Maybe<void>::Ok();
    })
    .SetGetSbpFn([](user_op::SbpContext* ctx) -> Maybe<void> {
      const user_op::TensorDesc& x1_tensor = ctx->LogicalTensorDesc4InputArgNameAndIndex("x1", 0);
      FOR_RANGE(int64_t, i, 0, x1_tensor.shape().NumAxes()) {
        ctx->NewBuilder().Split(ctx->inputs(), i).Split(ctx->outputs(), i).Build();
      }
      return Maybe<void>::Ok();
    });

REGISTER_USER_OP("TestMultiInputGrad")
    .Input("x1")
    .Input("x2")
    .Input("y_diff")
    .Output("x1_diff")
    .Output("x2_diff")
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      Shape* x1_shape = ctx->Shape4ArgNameAndIndex("x1", 0);
      Shape* x2_shape = ctx->Shape4ArgNameAndIndex("x2", 0);
      Shape* x1_diff_shape = ctx->Shape4ArgNameAndIndex("x1_diff", 0);
      Shape* x2_diff_shape = ctx->Shape4ArgNameAndIndex("x2_diff", 0);
      *x1_diff_shape = *x1_shape;
      *x2_diff_shape = *x2_shape;
      return Maybe<void>::Ok();
    })
    .SetGetSbpFn([](user_op::SbpContext* ctx) -> Maybe<void> {
      const user_op::TensorDesc& x1_tensor = ctx->LogicalTensorDesc4InputArgNameAndIndex("x1", 0);
      FOR_RANGE(int64_t, i, 0, x1_tensor.shape().NumAxes()) {
        ctx->NewBuilder().Split(ctx->inputs(), i).Split(ctx->outputs(), i).Build();
      }
      return Maybe<void>::Ok();
    });

REGISTER_USER_OP_GRAD("TestMultiInput")
    .SetGenBackwardOpConfFn([](const user_op::UserOpWrapper& op, user_op::AddOpFn AddOp) {
      if (op.NeedGenGradTensor4OpInput("x1", 0) || op.NeedGenGradTensor4OpInput("x2", 0)) {
        user_op::UserOpConfWrapperBuilder builder(op.op_name() + "_grad");
        user_op::UserOpConfWrapper test_multi_input_grad_op =
            builder.Op("TestMultiInputGrad")
                .Input("x1", op.input("x1", 0))
                .Input("x2", op.input("x2", 0))
                .Input("y_diff", op.GetGradTensorWithOpOutput("y", 0))
                .Output("x1_diff")
                .Output("x2_diff")
                .Build();
        op.BindGradTensorWithOpInput(test_multi_input_grad_op.output("x1_diff", 0), "x1", 0);
        op.BindGradTensorWithOpInput(test_multi_input_grad_op.output("x2_diff", 0), "x2", 0);
        AddOp(test_multi_input_grad_op);
      }
    });

REGISTER_USER_OP("TestDynamicSource")
    .Output("out")
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      // Shape* out_shape = ctx->Shape4ArgNameAndIndex("out", 0);
      // bool* is_dynamic = ctx->IsDynamic4ArgNameAndIndex("out", 0);
      // *is_dynamic = true;
      // *out_shape = Shape({5});
      // *ctx->Dtype4ArgNameAndIndex("out", 0) = DataType::kFloat;
      user_op::TensorDesc* out_tensor = ctx->TensorDesc4ArgNameAndIndex("out", 0);
      *out_tensor->mut_shape() = Shape({5});
      *out_tensor->mut_data_type() = DataType::kFloat;
      out_tensor->set_is_dynamic(true);
      return Maybe<void>::Ok();
    })
    .SetBatchAxisInferFn([](user_op::BatchAxisContext* ctx) -> Maybe<void> {
      ctx->BatchAxis4ArgNameAndIndex("out", 0)->set_value(0);
      return Maybe<void>::Ok();
    })
    .SetGetSbpFn([](user_op::SbpContext* ctx) -> Maybe<void> {
      ctx->NewBuilder().Split(ctx->outputs(), 0).Build();
      return Maybe<void>::Ok();
    });

REGISTER_USER_OP("TestRandomSource")
    .Output("out")
    .Attr("seed", UserOpAttrType::kAtInt64)
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      user_op::TensorDesc* out_tensor = ctx->TensorDesc4ArgNameAndIndex("out", 0);
      *out_tensor->mut_shape() = Shape({5});
      *out_tensor->mut_data_type() = DataType::kFloat;
      return Maybe<void>::Ok();
    });

REGISTER_USER_OP("TestDataTypeAttr")
    .Input("in")
    .Output("out")
    .Attr("output_type", UserOpAttrType::kAtDataType)
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      Shape* in_shape = ctx->Shape4ArgNameAndIndex("in", 0);
      Shape* out_shape = ctx->Shape4ArgNameAndIndex("out", 0);
      *out_shape = *in_shape;
      *ctx->Dtype4ArgNameAndIndex("out", 0) = ctx->GetAttr<DataType>("output_type");
      return Maybe<void>::Ok();
    });

REGISTER_USER_OP("TestListDataTypeAndListShapeAttr")
    .Input("in")
    .Output("out", 3)
    .Attr("out_shapes", UserOpAttrType::kAtListShape)
    .Attr("out_types", UserOpAttrType::kAtListDataType)
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      const auto& out_shapes = ctx->GetAttr<std::vector<Shape>>("out_shapes");
      const auto& out_types = ctx->GetAttr<std::vector<DataType>>("out_types");
      FOR_RANGE(int32_t, i, 0, ctx->outputs().size()) {
        *ctx->Shape4ArgNameAndIndex("out", i) = out_shapes.at(i);
        *ctx->Dtype4ArgNameAndIndex("out", i) = out_types.at(i);
      }
      return Maybe<void>::Ok();
    });

}  // namespace oneflow
