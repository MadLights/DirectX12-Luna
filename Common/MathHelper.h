//***************************************************************************************
// MathHelper.h by Frank Luna (C) 2011 All Rights Reserved.
//
// Helper math class.
//***************************************************************************************

#pragma once

#include <cstdlib>
#include <cmath>
#include <limits>

#include <DirectXMath.h>

namespace MathHelper
{
	// Returns random float in [0, 1).
	inline float RandF()
	{
		return static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
	}

	// Returns random float in [a, b).
	inline float RandF(float a, float b)
	{
		return a + RandF()*(b-a);
	}

    inline int Rand(int a, int b)
    {
        return a + rand() % ((b - a) + 1);
    }

	// Returns the polar angle of the point (x,y) in [0, 2*PI).
	float AngleFromXY(float x, float y);

	inline DirectX::XMVECTOR SphericalToCartesian(float radius, float theta, float phi)
	{
		const float sinPhi = std::sinf(phi);
		return DirectX::XMVectorSet(
			radius*sinPhi*cosf(theta),
			radius*cosf(phi),
			radius*sinPhi*sinf(theta),
			1.0f);
	}

    inline DirectX::XMMATRIX InverseTranspose(DirectX::CXMMATRIX M)
	{
		// Inverse-transpose is just applied to normals.  So zero out 
		// translation row so that it doesn't get into our inverse-transpose
		// calculation--we don't want the inverse-transpose of the translation.
        DirectX::XMMATRIX A = M;
        A.r[3] = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

        DirectX::XMVECTOR det = DirectX::XMMatrixDeterminant(A);
        return DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(&det, A));
	}

    constexpr DirectX::XMFLOAT4X4 Identity4x4()
    {
        return DirectX::XMFLOAT4X4(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f);
    }

    DirectX::XMVECTOR RandUnitVec3();
    DirectX::XMVECTOR RandHemisphereUnitVec3(DirectX::CXMVECTOR n);

	inline constexpr float Infinity = std::numeric_limits<float>::infinity();
	inline constexpr float Pi = DirectX::XM_PI;
};
