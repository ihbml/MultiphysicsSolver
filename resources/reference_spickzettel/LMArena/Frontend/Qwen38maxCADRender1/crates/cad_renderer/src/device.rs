use cad_plugin_api::{AdapterDetails, BackendPreference};
use thiserror::Error;
use wgpu::{
    Adapter, Backends, Device, DeviceDescriptor, DeviceType, ExperimentalFeatures, Features,
    Instance, InstanceDescriptor, InstanceFlags, Limits, MemoryHints, Queue, Surface,
    TextureFormat, Trace,
};

#[derive(Debug, Error)]
pub enum RendererError {
    #[error("No compatible DX12 or Vulkan GPU adapter found for preference {0:?}")]
    NoAdapter(BackendPreference),
    #[error("Failed to create wgpu device: {0}")]
    Device(#[from] wgpu::RequestDeviceError),
    #[error("Failed to create wgpu surface: {0}")]
    Surface(#[from] wgpu::CreateSurfaceError),
    #[error("Image export error: {0}")]
    Image(#[from] image::ImageError),
}

pub struct GpuContext {
    pub instance: Instance,
    pub adapter: Adapter,
    pub device: Device,
    pub queue: Queue,
    pub details: AdapterDetails,
    pub color_format: TextureFormat,
    pub depth_format: TextureFormat,
}

impl GpuContext {
    pub fn backends_for_preference(pref: BackendPreference) -> Backends {
        match pref {
            BackendPreference::VulkanAndDx12 => Backends::VULKAN | Backends::DX12,
            BackendPreference::VulkanOnly => Backends::VULKAN,
            BackendPreference::Dx12Only => Backends::DX12,
            BackendPreference::AutoAll => Backends::all(),
        }
    }

    pub fn new_headless(pref: BackendPreference) -> Result<Self, RendererError> {
        let backends = Self::backends_for_preference(pref);
        let instance = Instance::new(InstanceDescriptor {
            backends,
            flags: InstanceFlags::from_env_or_default(),
            ..InstanceDescriptor::new_without_display_handle()
        });
        Self::from_instance_and_surface(instance, None, pref)
    }

    pub fn from_instance_and_surface(
        instance: Instance,
        compatible_surface: Option<&Surface<'_>>,
        pref: BackendPreference,
    ) -> Result<Self, RendererError> {
        let backends = Self::backends_for_preference(pref);
        let mut adapters = pollster::block_on(instance.enumerate_adapters(backends));

        // If DX12+Vulkan requested and none found (e.g. macOS Metal), allow AutoAll fallback
        let used_fallback_backends = if adapters.is_empty() && pref == BackendPreference::VulkanAndDx12 {
            adapters = pollster::block_on(instance.enumerate_adapters(Backends::all()));
            true
        } else {
            false
        };

        let available_adapters: Vec<String> = adapters
            .iter()
            .map(|a| {
                let info = a.get_info();
                format!("{} ({:?}, {:?})", info.name, info.backend, info.device_type)
            })
            .collect();

        let adapter = select_best_adapter(adapters, compatible_surface)
            .ok_or(RendererError::NoAdapter(pref))?;

        let info = adapter.get_info();
        let color_format = TextureFormat::Rgba8Unorm;
        let depth_format = TextureFormat::Depth32Float;

        let color_msaa = adapter
            .get_texture_format_features(color_format)
            .flags
            .sample_count_supported(4);
        let depth_msaa = adapter
            .get_texture_format_features(depth_format)
            .flags
            .sample_count_supported(4);
        let msaa_samples = if color_msaa && depth_msaa { 4 } else { 1 };

        let (device, queue) = pollster::block_on(adapter.request_device(&DeviceDescriptor {
            label: Some("cad_renderer_device"),
            required_features: Features::empty(),
            required_limits: Limits::downlevel_defaults().using_resolution(adapter.limits()),
            experimental_features: ExperimentalFeatures::disabled(),
            memory_hints: MemoryHints::Performance,
            trace: Trace::Off,
        }))?;

        let backend_label = if used_fallback_backends {
            format!("{:?} (Fallback)", info.backend)
        } else {
            format!("{:?}", info.backend)
        };

        let details = AdapterDetails {
            adapter_name: info.name.clone(),
            backend_name: backend_label,
            driver: info.driver.clone(),
            driver_info: info.driver_info.clone(),
            device_type: format!("{:?}", info.device_type),
            vendor_id: info.vendor,
            device_id: info.device,
            msaa_samples,
            reverse_z_enabled: true,
            available_adapters,
        };

        Ok(Self {
            instance,
            adapter,
            device,
            queue,
            details,
            color_format,
            depth_format,
        })
    }
}

fn device_type_rank(dt: DeviceType) -> u8 {
    match dt {
        DeviceType::DiscreteGpu => 0,
        DeviceType::IntegratedGpu => 1,
        DeviceType::VirtualGpu => 2,
        DeviceType::Cpu => 3,
        DeviceType::Other => 4,
    }
}

fn backend_rank(backend: wgpu::Backend) -> u8 {
    match backend {
        wgpu::Backend::Vulkan => 0,
        wgpu::Backend::Dx12 => 0,
        wgpu::Backend::Metal => 1,
        _ => 2,
    }
}

fn select_best_adapter(
    mut adapters: Vec<Adapter>,
    compatible_surface: Option<&Surface<'_>>,
) -> Option<Adapter> {
    adapters.retain(|a| {
        compatible_surface.is_none_or(|surf| a.is_surface_supported(surf))
    });
    adapters.sort_by_key(|a| {
        let info = a.get_info();
        (device_type_rank(info.device_type), backend_rank(info.backend))
    });
    adapters.into_iter().next()
}
