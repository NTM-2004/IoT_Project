package com.iot.parking.controller;

import java.time.LocalDateTime;
import java.util.Optional;

import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import com.iot.parking.model.Device;
import com.iot.parking.repository.DeviceRepository;

@RestController
@RequestMapping("/api/device")
public class DeviceController {
	
	@Autowired
	private DeviceRepository repo;

	public DeviceController(DeviceRepository repo) {
		super();
		this.repo = repo;
	}
	
	@PostMapping("/register")
	public ResponseEntity<?> register(@RequestBody Device d) {
		Optional<Device> exist = repo.findByDeviceId(d.getDeviceId());
		Device device;
		if (exist.isPresent()) {
			device = exist.get();
			device.setMac(d.getMac());
			device.setIp(d.getIp());
			device.setLastSeen(LocalDateTime.now());
			device.setStatus("online");
		} else {
			device = new Device();
			device.setDeviceId(d.getDeviceId());
			device.setMac(d.getMac());
			device.setIp(d.getIp());
			device.setStatus("online");
			device.setLastSeen(LocalDateTime.now());
		}
		Device saved = repo.save(device);
		return ResponseEntity.status(201).body(saved);
	}


	@PostMapping("/heartbeat")
	public ResponseEntity<?> heartbeat(@RequestBody Device d) {
		Optional<Device> exist = repo.findByDeviceId(d.getDeviceId());
		if (exist.isPresent()) {
			Device device = exist.get();
			device.setLastSeen(LocalDateTime.now());
			device.setStatus(d.getStatus() != null ? d.getStatus() : "online");
			device.setIp(d.getIp());
			repo.save(device);
			return ResponseEntity.ok(device);
		} else {
			return ResponseEntity.status(404).body("Device not registered");
		}
	}


	@GetMapping("/all")
	public ResponseEntity<?> getAll() {
		return ResponseEntity.ok(repo.findAll());
	}
}
