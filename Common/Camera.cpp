// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include "Camera.h"

using namespace DirectX;

Camera::Camera()
{
    SetLens(0.25f * MathHelper::Pi, 1.0f, 1.0f, 1000.0f);
}

XMVECTOR Camera::GetPosition()const
{
    return XMLoadFloat3(&mPosition);
}

XMFLOAT3 Camera::GetPosition3f()const
{
    return mPosition;
}

void Camera::SetPosition(float x, float y, float z)
{
    mPosition = XMFLOAT3(x, y, z);
    mViewDirty = true;
}

void Camera::SetPosition(const XMFLOAT3& v)
{
    mPosition = v;
    mViewDirty = true;
}

XMVECTOR Camera::GetRight()const
{
    return XMLoadFloat3(&mRight);
}

XMFLOAT3 Camera::GetRight3f()const
{
    return mRight;
}

XMVECTOR Camera::GetUp()const
{
    return XMLoadFloat3(&mUp);
}

XMFLOAT3 Camera::GetUp3f()const
{
    return mUp;
}

XMVECTOR Camera::GetLook()const
{
    return XMLoadFloat3(&mLook);
}

XMFLOAT3 Camera::GetLook3f()const
{
    return mLook;
}

float Camera::GetNearZ()const
{
    return mNearZ;
}

float Camera::GetFarZ()const
{
    return mFarZ;
}

float Camera::GetAspect()const
{
    return mAspect;
}

float Camera::GetFovY()const
{
    return mFovY;
}

float Camera::GetFovX()const
{
    const float halfWidth = 0.5f * GetNearWindowWidth();
    return 2.0f * atanf(halfWidth / mNearZ);
}

float Camera::GetNearWindowWidth()const
{
    return mAspect * mNearWindowHeight;
}

float Camera::GetNearWindowHeight()const
{
    return mNearWindowHeight;
}

float Camera::GetFarWindowWidth()const
{
    return mAspect * mFarWindowHeight;
}

float Camera::GetFarWindowHeight()const
{
    return mFarWindowHeight;
}

void Camera::SetLens(
    const float fovY,
    const float aspect,
    const float zn,
    const float zf)
{
    // cache properties
    mFovY = fovY;
    mAspect = aspect;
    mNearZ = zn;
    mFarZ = zf;

    const float t = std::tanf(0.5f * mFovY);

    mNearWindowHeight = 2.0f * mNearZ * t;
    mFarWindowHeight = 2.0f * mFarZ * t;
    XMStoreFloat4x4(
        &mProj, 
        XMMatrixPerspectiveFovLH(mFovY, mAspect, mNearZ, mFarZ));
}

void Camera::LookAt(FXMVECTOR pos, FXMVECTOR target, FXMVECTOR worldUp)
{
    const XMVECTOR L = XMVector3Normalize(XMVectorSubtract(target, pos));
    const XMVECTOR R = XMVector3Normalize(XMVector3Cross(worldUp, L));
    const XMVECTOR U = XMVector3Cross(L, R);

    XMStoreFloat3(&mPosition, pos);
    XMStoreFloat3(&mLook, L);
    XMStoreFloat3(&mRight, R);
    XMStoreFloat3(&mUp, U);

    mViewDirty = true;
}

void Camera::LookAt(const XMFLOAT3& pos, const XMFLOAT3& target, const XMFLOAT3& up)
{
    const XMVECTOR P = XMLoadFloat3(&pos);
    const XMVECTOR T = XMLoadFloat3(&target);
    const XMVECTOR U = XMLoadFloat3(&up);
    LookAt(P, T, U);
}

XMMATRIX Camera::GetView()const
{
    assert(!mViewDirty);
    return XMLoadFloat4x4(&mView);
}

XMMATRIX Camera::GetProj()const
{
    return XMLoadFloat4x4(&mProj);
}


XMFLOAT4X4 Camera::GetView4x4f()const
{
    assert(!mViewDirty);
    return mView;
}

XMFLOAT4X4 Camera::GetProj4x4f()const
{
    return mProj;
}

void Camera::Strafe(const float d)
{
    // pos += d * right.
    XMStoreFloat3(
        &mPosition, 
        XMVectorMultiplyAdd(
            XMVectorReplicate(d), 
            XMLoadFloat3(&mRight), 
            XMLoadFloat3(&mPosition))
    );

    mViewDirty = true;
}

void Camera::Walk(const float d)
{
    // mPosition += d * mLook
    XMStoreFloat3(
        &mPosition, 
        XMVectorMultiplyAdd(
            XMVectorReplicate(d),
            XMLoadFloat3(&mLook),
            XMLoadFloat3(&mPosition))
    );

    mViewDirty = true;
}

void Camera::Pitch(const float angle)
{
    // Rotate up and look vector about the right vector.

    const XMMATRIX R = XMMatrixRotationAxis(XMLoadFloat3(&mRight), angle);

    XMStoreFloat3(&mUp, XMVector3TransformNormal(XMLoadFloat3(&mUp), R));
    XMStoreFloat3(&mLook, XMVector3TransformNormal(XMLoadFloat3(&mLook), R));

    mViewDirty = true;
}

void Camera::RotateY(const float angle)
{
    // Rotate the basis vectors about the world y-axis.

    const XMMATRIX R = XMMatrixRotationY(angle);

    XMStoreFloat3(&mRight, XMVector3TransformNormal(XMLoadFloat3(&mRight), R));
    XMStoreFloat3(&mUp, XMVector3TransformNormal(XMLoadFloat3(&mUp), R));
    XMStoreFloat3(&mLook, XMVector3TransformNormal(XMLoadFloat3(&mLook), R));

    mViewDirty = true;
}

void Camera::UpdateViewMatrix()
{
    if (mViewDirty)
    {
        XMVECTOR R = XMLoadFloat3(&mRight);
        XMVECTOR U = XMLoadFloat3(&mUp);
        XMVECTOR L = XMLoadFloat3(&mLook);
        const XMVECTOR P = XMLoadFloat3(&mPosition);

        // Keep camera's axes orthogonal to each other and of unit length.
        L = XMVector3Normalize(L);
        U = XMVector3Normalize(XMVector3Cross(L, R));

        // U, L already ortho-normal, so no need to normalize cross product.
        R = XMVector3Cross(U, L);

        // Fill in the view matrix entries.
        const float x = -XMVectorGetX(XMVector3Dot(P, R));
        const float y = -XMVectorGetX(XMVector3Dot(P, U));
        const float z = -XMVectorGetX(XMVector3Dot(P, L));

        XMStoreFloat3(&mRight, R);
        XMStoreFloat3(&mUp, U);
        XMStoreFloat3(&mLook, L);

        mView(0, 0) = mRight.x;
        mView(1, 0) = mRight.y;
        mView(2, 0) = mRight.z;
        mView(3, 0) = x;

        mView(0, 1) = mUp.x;
        mView(1, 1) = mUp.y;
        mView(2, 1) = mUp.z;
        mView(3, 1) = y;

        mView(0, 2) = mLook.x;
        mView(1, 2) = mLook.y;
        mView(2, 2) = mLook.z;
        mView(3, 2) = z;

        mView(0, 3) = 0.0f;
        mView(1, 3) = 0.0f;
        mView(2, 3) = 0.0f;
        mView(3, 3) = 1.0f;

        mViewDirty = false;
    }
}
