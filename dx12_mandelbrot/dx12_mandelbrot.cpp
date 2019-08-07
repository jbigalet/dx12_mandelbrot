// dx12_mandelbrot.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <Windows.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

constexpr int BUFFER_COUNT = 3;

void unreachable() {
	throw std::exception();
}

void assert(bool b) {
	if (!b)
		unreachable();
}

void check_hr(HRESULT hr) {
	assert(!FAILED(hr));
}

long long time_init = 0;
double freq = 1;
double get_time() {
	long long t;
	QueryPerformanceCounter((LARGE_INTEGER*)&t);
	return (t - time_init) / freq;
}

bool should_close = false;

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	//auto ptr = GetWindowLongPtr(hwnd, GWLP_USERDATA);
	
	switch (uMsg) {
	case WM_DESTROY:
		PostQuitMessage(0);
		should_close = true;
		return 0;
	}

	return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

void wait_for_fence(ID3D12Fence* fence, UINT64 completion_value, HANDLE wait_event) {
	if (fence->GetCompletedValue() < completion_value) {
		fence->SetEventOnCompletion(completion_value, wait_event);
		WaitForSingleObject(wait_event, INFINITE);
	}
}

bool compile_shader(LPCWSTR filename, LPCSTR entrypoint, LPCSTR type, ID3DBlob** out) {
	ID3DBlob* error_blob = NULL;
	ID3DBlob* shader_blob = NULL;
	int shader_flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG;
	HRESULT hr = D3DCompileFromFile(filename, NULL, NULL, entrypoint, type, shader_flags, 0, &shader_blob, &error_blob);

	if (FAILED(hr)) {
		if (error_blob) {
			printf("error compiling vs: %s\n", (char*)error_blob->GetBufferPointer());
			error_blob->Release();
		} else {
			printf("unknown error compiling vs\n");
		}
		return false;
	}
	*out = shader_blob;
	return true;
}

void create_pso(ComPtr<ID3D12Device> device, ComPtr<ID3D12RootSignature> root_signature, ID3D12PipelineState** pso) {
	ID3DBlob *vertex_shader, *pixel_shader;
	while (!compile_shader(L"shader.hlsl", "VS_main", "vs_5_0", &vertex_shader))
		Sleep(1000);
	while (!compile_shader(L"shader.hlsl", "PS_main", "ps_5_0", &pixel_shader))
		Sleep(1000);

	static const D3D12_INPUT_ELEMENT_DESC layout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.VS.BytecodeLength = vertex_shader->GetBufferSize();
	psoDesc.VS.pShaderBytecode = vertex_shader->GetBufferPointer();
	psoDesc.PS.BytecodeLength = pixel_shader->GetBufferSize();
	psoDesc.PS.pShaderBytecode = pixel_shader->GetBufferPointer();
	psoDesc.pRootSignature = root_signature.Get();
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	psoDesc.InputLayout.NumElements = std::extent<decltype(layout)>::value;
	psoDesc.InputLayout.pInputElementDescs = layout;
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.BlendState.RenderTarget[0].BlendEnable = false;
	//psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	//psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	//psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	//psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	//psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	//psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	//psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.DepthStencilState.DepthEnable = false;
	psoDesc.DepthStencilState.StencilEnable = false;
	psoDesc.SampleMask = 0xFFFFFFFF;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	check_hr(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pso)));
}

double last_reload_t = 0.f;
time_t last_mtime = {};
void reload_shaders(ComPtr<ID3D12Device> device, ComPtr<ID3D12RootSignature> root_signature, ID3D12PipelineState** pso) {
	if(get_time()-last_reload_t > 1.f) {
		struct stat st;
		stat("shader.hlsl", &st);
		if (st.st_mtime != last_mtime) {
			last_mtime = st.st_mtime;
			printf("reloading shaders\n");
			last_reload_t = get_time();
			//(*pso)->Release();
			create_pso(device, root_signature, pso);
		}
	}
}

int main() {
	long long _freq;
	bool has_perf_counter = QueryPerformanceFrequency((LARGE_INTEGER*)&_freq);
	assert(has_perf_counter);
	freq = (double)_freq;
	QueryPerformanceCounter((LARGE_INTEGER*)&time_init);


    //
	// dx12 init
	//

	// create window & window class

	DWORD style = WS_OVERLAPPEDWINDOW;
	int width = 1280;
	int height = 1280;
	RECT rect = { 0, 0, width, height };
	AdjustWindowRect(&rect, style, false);

	WNDCLASSA window_class;
	window_class.style = CS_HREDRAW | CS_VREDRAW;
	window_class.lpfnWndProc = wnd_proc;
	window_class.cbClsExtra = 0;
	window_class.cbWndExtra = 0;
	window_class.hInstance = GetModuleHandle(NULL);
	window_class.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
	window_class.hbrBackground = (HBRUSH)COLOR_WINDOW;
	window_class.lpszMenuName = NULL;
	window_class.lpszClassName = "MandelbrotWindowClass";
	RegisterClassA(&window_class);

	HWND window = CreateWindowA(window_class.lpszClassName, "Mandelbrot", style, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, GetModuleHandle(NULL), NULL);
	//SetWindowLongPtr(window, GWLP_USERDATA, ...);
	ShowWindow(window, 1);


	// device & swapchain

#if 1
	ComPtr<ID3D12Debug> dbg_interface;
	D3D12GetDebugInterface(IID_PPV_ARGS(&dbg_interface));
	dbg_interface->EnableDebugLayer();
#endif

	ComPtr<ID3D12Device> device;
	check_hr(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));

	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	ComPtr<ID3D12CommandQueue> queue;
	check_hr(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)));

	ComPtr<IDXGIFactory4> factory;
	check_hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));
	swapChainDesc.BufferCount = BUFFER_COUNT;
	// This is _UNORM but we'll use a _SRGB view on this. See 
	// SetupRenderTargets() for details, it must match what
	// we specify here
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferDesc.Width = width;
	swapChainDesc.BufferDesc.Height = height;
	swapChainDesc.OutputWindow = window;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.Windowed = true;
	ComPtr<IDXGISwapChain> swap_chain;
	check_hr(factory->CreateSwapChain(queue.Get(), &swapChainDesc, &swap_chain));

	int RTV_desc_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);


	// setup swap chain & render targets

	int current_fence_value = 1;

	// Create fences for each frame so we can protect resources and wait for any given frame
	HANDLE frame_fence_events[BUFFER_COUNT];
	ComPtr<ID3D12Fence> frame_fences[BUFFER_COUNT];
	UINT64 fence_values[BUFFER_COUNT];
	for (int i = 0; i < BUFFER_COUNT ; ++i) {
		frame_fence_events[i] = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		fence_values[i] = 0;
		device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&frame_fences[i]));
	}

	ComPtr<ID3D12Resource> render_targets[BUFFER_COUNT];
	for (int i = 0; i < BUFFER_COUNT ; ++i)
		swap_chain->GetBuffer(i, IID_PPV_ARGS(&render_targets[i]));


	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = BUFFER_COUNT;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	ComPtr<ID3D12DescriptorHeap> desc_heap;
	device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&desc_heap));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle { desc_heap->GetCPUDescriptorHandleForHeapStart() };

	for (int i = 0; i < BUFFER_COUNT ; ++i) {
		D3D12_RENDER_TARGET_VIEW_DESC viewDesc;
		viewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		viewDesc.Texture2D.MipSlice = 0;
		viewDesc.Texture2D.PlaneSlice = 0;

		device->CreateRenderTargetView(render_targets[i].Get(), &viewDesc, rtvHandle);

		rtvHandle.Offset(RTV_desc_size);
	}


	// allocators & command lists

	ComPtr<ID3D12CommandAllocator> cmd_allocators[BUFFER_COUNT];
	ComPtr<ID3D12GraphicsCommandList> cmd_lists[BUFFER_COUNT];
	for (int i = 0; i < BUFFER_COUNT; ++i) {
		device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmd_allocators[i]));
		device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_allocators[i].Get(), nullptr, IID_PPV_ARGS(&cmd_lists[i]));
		cmd_lists[i]->Close();
	}

	
	D3D12_RECT scissor = { 0, 0, width, height };
	D3D12_VIEWPORT viewport = {
		0.0f, 0.0f,
		(float)width, (float)height,
		0.0f, 1.0f
	};

	// Create our upload fence, command list and command allocator
	// This will be only used while creating the mesh buffer and the texture to upload data to the GPU.
	ComPtr<ID3D12Fence> upload_fence;
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&upload_fence));

	ComPtr<ID3D12CommandAllocator> upload_cmd_allocator;
	device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&upload_cmd_allocator));
	ComPtr<ID3D12GraphicsCommandList> upload_cmd_list;
	device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, upload_cmd_allocator.Get(), nullptr, IID_PPV_ARGS(&upload_cmd_list));


	// root signature

	// We have two root parameters, one is a pointer to a descriptor heap with a SRV, the second is a constant buffer view
	CD3DX12_ROOT_PARAMETER parameters[1];
	parameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);  // Our constant buffer view

	CD3DX12_ROOT_SIGNATURE_DESC descRootSignature;
	descRootSignature.Init(1, parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	ComPtr<ID3DBlob> rootBlob;
	ComPtr<ID3DBlob> errorBlob;
	D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &errorBlob);
	ComPtr<ID3D12RootSignature> root_signature;
	device->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(), IID_PPV_ARGS(&root_signature));


	ID3D12PipelineState* pso;
	create_pso(device, root_signature, &pso);


	// mesh buffers

	struct Vertex
	{
		float position[3];
		float uv[2];
	};

	// Declare upload buffer data as 'static' so it persists after returning from this function.
	// Otherwise, we would need to explicitly wait for the GPU to copy data from the upload buffer
	// to vertex/index default buffers due to how the GPU processes commands asynchronously. 
	static const Vertex vertices[4] = {
		// Upper Left
		{ { -1.0f, 1.0f, 0 },{ 0, 0 } },
		// Upper Right
		{ { 1.0f, 1.0f, 0 },{ 1, 0 } },
		// Bottom right
		{ { 1.0f, -1.0f, 0 },{ 1, 1 } },
		// Bottom left
		{ { -1.0f, -1.0f, 0 },{ 0, 1 } }
	};

	static const int indices[6] = {
		0, 1, 2, 2, 3, 0
	};

	static const int uploadBufferSize = sizeof(vertices) + sizeof(indices);
	static const auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	static const auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

	// Create upload buffer on CPU
	ComPtr<ID3D12Resource> upload_buffer;
	device->CreateCommittedResource(&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&uploadBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&upload_buffer));

	// Create vertex & index buffer on the GPU
	// HEAP_TYPE_DEFAULT is on GPU, we also initialize with COPY_DEST state
	// so we don't have to transition into this before copying into them
	static const auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	ComPtr<ID3D12Resource> vertex_buffer;
	static const auto vertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(vertices));
	device->CreateCommittedResource(&defaultHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&vertexBufferDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&vertex_buffer));

	ComPtr<ID3D12Resource> index_buffer;
	static const auto indexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(indices));
	device->CreateCommittedResource(&defaultHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&indexBufferDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&index_buffer));

	// Create buffer views
	D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view;
	vertex_buffer_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
	vertex_buffer_view.SizeInBytes = sizeof(vertices);
	vertex_buffer_view.StrideInBytes = sizeof(Vertex);

	D3D12_INDEX_BUFFER_VIEW index_buffer_view;
	index_buffer_view.BufferLocation = index_buffer->GetGPUVirtualAddress();
	index_buffer_view.SizeInBytes = sizeof(indices);
	index_buffer_view.Format = DXGI_FORMAT_R32_UINT;

	// Copy data on CPU into the upload buffer
	void* p;
	upload_buffer->Map(0, nullptr, &p);
	memcpy(p, vertices, sizeof(vertices));
	memcpy((unsigned char*)p + sizeof(vertices), indices, sizeof(indices));
	upload_buffer->Unmap(0, nullptr);

	// Copy data from upload buffer on CPU into the index/vertex buffer on the GPU
	upload_cmd_list->CopyBufferRegion(vertex_buffer.Get(), 0, upload_buffer.Get(), 0, sizeof(vertices));
	upload_cmd_list->CopyBufferRegion(index_buffer.Get(), 0, upload_buffer.Get(), sizeof(vertices), sizeof(indices));

	// Barriers, batch them together
	const CD3DX12_RESOURCE_BARRIER barriers[2] = {
		CD3DX12_RESOURCE_BARRIER::Transition(vertex_buffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
		CD3DX12_RESOURCE_BARRIER::Transition(index_buffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER)
	};

	upload_cmd_list->ResourceBarrier(2, barriers);


	// finalize the upload & cleanup

	upload_cmd_list->Close();

	// Execute the upload and finish the command list
	ID3D12CommandList* upload_cmd_lists[] = { upload_cmd_list.Get() };
	queue->ExecuteCommandLists(std::extent<decltype(upload_cmd_lists)>::value, upload_cmd_lists);
	queue->Signal(upload_fence.Get(), 1);

	auto wait_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	assert(wait_event != NULL);

	wait_for_fence(upload_fence.Get(), 1, wait_event);

	upload_cmd_allocator->Reset();
	CloseHandle(wait_event);


	int current_back_buffer = 0;
	double t = 0.f;
	while (!should_close) {
		wait_for_fence(frame_fences[current_back_buffer].Get(), fence_values[current_back_buffer], frame_fence_events[current_back_buffer]);

		reload_shaders(device, root_signature, &pso);

		MSG msg;
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			if (msg.message == WM_QUIT)
				exit(0);
		}

		double new_t = get_time();
		double dt = new_t - t;
		printf("frame time: %f\n", dt);
		t = new_t;

		cmd_allocators[current_back_buffer]->Reset();

		auto commandList = cmd_lists[current_back_buffer].Get();
		commandList->Reset(
			cmd_allocators[current_back_buffer].Get(), nullptr);

		D3D12_CPU_DESCRIPTOR_HANDLE renderTargetHandle;
		CD3DX12_CPU_DESCRIPTOR_HANDLE::InitOffsetted(renderTargetHandle,
			desc_heap->GetCPUDescriptorHandleForHeapStart(),
			current_back_buffer, RTV_desc_size);

		commandList->OMSetRenderTargets(1, &renderTargetHandle, true, nullptr);
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissor);

		// Transition back buffer
		{
			D3D12_RESOURCE_BARRIER barrier;
			barrier.Transition.pResource = render_targets[current_back_buffer].Get();
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			commandList->ResourceBarrier(1, &barrier);
		}

		static const float clearColor[] = {
			0.042f, 0.042f, 0.042f,
			1
		};

		commandList->ClearRenderTargetView(renderTargetHandle, clearColor, 0, nullptr);

		auto cmd_list = cmd_lists[current_back_buffer].Get();
		cmd_list->SetPipelineState(pso);
		cmd_list->SetGraphicsRootSignature(root_signature.Get());
		cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		cmd_list->IASetVertexBuffers(0, 1, &vertex_buffer_view);
		cmd_list->IASetIndexBuffer(&index_buffer_view);
		cmd_list->DrawIndexedInstanced(6, 1, 0, 0, 0);

		// Transition the swap chain back to present
		auto current_cmd_list = cmd_lists[current_back_buffer].Get(); 
		{
			D3D12_RESOURCE_BARRIER barrier;
			barrier.Transition.pResource = render_targets[current_back_buffer].Get();
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			current_cmd_list->ResourceBarrier(1, &barrier);
		}

		current_cmd_list->Close();

		// Execute our commands
		ID3D12CommandList* current_cmd_lists[] = { current_cmd_list };
		queue->ExecuteCommandLists(std::extent<decltype(current_cmd_lists)>::value, current_cmd_lists);

		swap_chain->Present(1, 0);

		// Mark the fence for the current frame.
		const auto fenceValue = current_fence_value;
		queue->Signal(frame_fences[current_back_buffer].Get(), fenceValue);
		fence_values[current_back_buffer] = fenceValue;
		current_fence_value++;

		current_back_buffer = (current_back_buffer + 1) % BUFFER_COUNT;
	}


	return 0;
}