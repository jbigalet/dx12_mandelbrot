struct VertexShaderOutput {
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD;
};

VertexShaderOutput VS_main(float4 position : POSITION, float2 uv : TEXCOORD) {
	VertexShaderOutput output;

	output.position = position;
	output.uv = uv;

	return output;
}



cbuffer ConstantBuffer : register(b0) {
	float t;
};

#define CONTINUOUS

static const int MAX_IT = 1000;

#ifdef CONTINUOUS
static const int escape = (1 << 8);
#else
static const int escape = 2;
#endif

static const int escape2 = escape * escape;
static const float rlog2 = 1.f / log(2);

float3 colorize(int it) {
#if 1
	static const int palette_size = 16;
	static const float3 palette[palette_size] = {
		float3(241, 233, 191) / 255.f,
		float3(248, 201, 95) / 255.f,
		float3(255, 170, 0) / 255.f,
		float3(204, 128, 0) / 255.f,
		float3(153, 87, 0) / 255.f,
		float3(106, 52, 3) / 255.f,
		float3(66, 30, 15) / 255.f,
		float3(25, 7, 26) / 255.f,
		float3(9, 1, 47) / 255.f,
		float3(4, 4, 73) / 255.f,
		float3(0, 7, 100) / 255.f,
		float3(12, 44, 138) / 255.f,
		float3(24, 82, 177) / 255.f,
		float3(57, 125, 209) / 255.f,
		float3(134, 181, 229) / 255.f,
		float3(211, 236, 248) / 255.f,
	};
	if (it == MAX_IT)
		return 0.f.xxx;
	return palette[(it+7) % palette_size];
#else
	return it / (float)MAX_IT;
#endif
}

float4 PS_main (float4 position : SV_POSITION, float2 uv : TEXCOORD) : SV_TARGET {
	//float x_min = -2.1f, x_max = 0.9f;
	//float y_min = -1.5f, y_max = 1.5f;

#if 1
	float2 center = float2(-0.74526f, -0.09977f);
	//const float min_scaling = 1.5f, max_scaling = 0.00001f;
	const float min_scaling = 2.f;
	const float scaling_per_sec = 0.5f;
	const float total_scaling_time = 17.f;
	float T = fmod(t, total_scaling_time*2);
	if (T > total_scaling_time) T = T-2*(T-total_scaling_time);
	float scaling = min_scaling * pow(scaling_per_sec, fmod(T, total_scaling_time));
#else
	float2 center = float2(-0.6f, 0.f);
	float scaling = 1.5f;  // base is -1:1
#endif
	float x_min = center.x - scaling, x_max = center.x + scaling;
	float y_min = center.y - scaling, y_max = center.y + scaling;

	float2 pos = float2(x_min + uv.x*(x_max - x_min), y_min + uv.y*(y_max - y_min));
	
	float zr = 0.f, zi = 0.f;
	int it = 0;
	while (zr*zr + zi * zi <= escape2 && it < MAX_IT) {
		// z(n+1) = z(n)^2 + c
		float new_zr = zr * zr - zi * zi + pos.x;
		zi = 2 * zi * zr + pos.y;
		zr = new_zr;
		it++;
	}

#ifdef CONTINUOUS
	float f_it = it;
	if (it < MAX_IT) {
		float log_zn = log(zr*zr + zi * zi) / 2;
		float nu = log(log_zn * rlog2) * rlog2;
		f_it = f_it + 1 - nu;
	}
	float3 c1 = colorize(floor(f_it));
	float3 c2 = colorize(floor(f_it + 1));
	float3 c = lerp(c1, c2, fmod(f_it, 1.f));

#else
	float3 c = colorize(it);
#endif

	return float4(c, 1);
}
