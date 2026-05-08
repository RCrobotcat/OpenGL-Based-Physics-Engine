#include "hingeConstraint.h"
#include "constraintImpl.h"

namespace P3D {
    int HingeConstraint::maxNumberOfParameters() const {
        return 5;
    }

    static ConstraintMatrixPair<5> makeMatrices(const PhysicalInfo &phys, const Vec3 &attach, const Vec3 &p1,
                                                const Vec3 &p2) {
        // 铰链连接点 attach 转成刚体局部相对坐标，然后减去质心位置
        // 得到 r = attach - COM，用于后续计算力矩
        Vec3 attachRelativeToCOM = phys.cframe.localToRelative(attach) - phys.relativeCenterOfMass;

        // get [r]x, 用于把叉乘运算变成矩阵乘法
        // s. t. [r]x * F(矩阵乘法) == r % F(叉乘) == N(力矩)
        Mat3 crossEquivAttach = createCrossProductEquivalent(attachRelativeToCOM);

        // 1/m 0 0 | 0 0
        // 0 1/m 0 | 0 0
        // 0 0 1/m | 0 0
        Matrix<double, 3, 5> impulseToMotion = joinHorizontal(Mat3::DIAGONAL(phys.forceResponse),
                                                              Matrix<double, 3, 2>::ZEROS());
        // r00 r01 r02 | p1x p2x
        // r10 r11 r12 | p1y p2y
        // r20 r21 r22 | p1z p2z
        Matrix<double, 3, 5> angularEffect = joinHorizontal(crossEquivAttach,
                                                            Matrix<double, 3, 2>::fromColumns({p1, p2}));

        // 刚体运动变化 [Δv, Δω]^T
        // 上半部分是线速度响应 Δv = F / m
        // 下半部分是角速度响应 Δω = I^{-1} N
        // 等同于 M^{-1}J^T
        Matrix<double, 6, 5> parameterToMotion = joinVertical(impulseToMotion, phys.momentResponse * angularEffect);

        // 雅可比矩阵 J
        Matrix<double, 5, 6> motionToEquation = join(Mat3::IDENTITY(), -crossEquivAttach,
                                                     Matrix<double, 2, 3>::ZEROS(),
                                                     Matrix<double, 2, 3>::fromRows({p1, p2}));

        return ConstraintMatrixPair<5>{parameterToMotion, motionToEquation};
    }

    /// <summary>
    /// 几何约束: 两个刚体通过铰链连接，只允许绕同一根轴相对旋转.
    /// 这个函数将几何约束转换成 5 维约束方程及其雅可比矩阵，供物理求解器计算修正冲量
    /// </summary>
    ConstraintMatrixPack HingeConstraint::getMatrices(const PhysicalInfo &physA, const PhysicalInfo &physB,
                                                      double *matrixBuf, double *errorBuf) const {
        // 构造局部坐标系
        Vec3 localMainAxis = normalize(this->axisA);
        Vec3 localP1 = normalize(getPerpendicular(localMainAxis));
        Vec3 localP2 = normalize(localMainAxis % localP1);

        Vec3 localMainOffsetAxis = normalize(this->axisB);

        // to world
        Vec3 mainAxis = physA.cframe.localToRelative(localMainAxis);
        Vec3 p1 = physA.cframe.localToRelative(localP1);
        Vec3 p2 = physA.cframe.localToRelative(localP2);
        Vec3 mainOffsetAxis = physB.cframe.localToRelative(localMainOffsetAxis);

        // rotationOffset will be in the plane defined by p1 and p2, as it is perpendicular to mainAxis
        // 两根单位方向轴AB之间的角度偏离
        // |mainAxis| * |mainOffsetAxis| * sin(θ) == sin(θ)
        Vec3 rotationOffset = mainAxis % mainOffsetAxis;
        // 投影到 p1 和 p2 上，得到在约束方程中的误差分量
        double rotationOffsetP1 = p1 * rotationOffset;
        double rotationOffsetP2 = p2 * rotationOffset;

        ConstraintMatrixPair<5> cA = makeMatrices(physA, attachA, p1, p2);
        ConstraintMatrixPair<5> cB = makeMatrices(physB, attachB, p1, p2);

        // position_error_x
        // position_error_y
        // position_error_z
        // angular_error_p1
        // angular_error_p2
        Vec5 error0 = join(Vec3(physB.cframe.localToGlobal(attachB) - physA.cframe.localToGlobal(attachA)),
                           Vec2(rotationOffsetP1, rotationOffsetP2));

        // 雅可比约束形式 J * V
        Vec5 velocityA = cA.motionToEquation * physA.motion.getDerivAsVec6(0);
        Vec5 velocityB = cB.motionToEquation * physB.motion.getDerivAsVec6(0);
        Vec5 error1 = velocityB - velocityA;

        Matrix<double, 5, NUMBER_OF_ERROR_DERIVATIVES> error = Matrix<double, 5,
            NUMBER_OF_ERROR_DERIVATIVES>::fromColumns({error0, error1});

        return ConstraintMatrixPack(matrixBuf, errorBuf, cA, cB, error);
    }
};
