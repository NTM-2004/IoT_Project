package com.iot.parking.controller;

import java.time.LocalDateTime;
import java.util.List;

import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.ResponseEntity;
import org.springframework.validation.annotation.Validated;
import org.springframework.web.bind.annotation.*;

import com.iot.parking.model.Sensor;
import com.iot.parking.repository.SensorRepository;

@RestController
@RequestMapping("/api/sensor")
public class SensorController {
	
	@Autowired
	private SensorRepository repo;

	public SensorController(SensorRepository repo) {
		super();
		this.repo = repo;
	}
	
	//nhận record
	@PostMapping("/data")
	public ResponseEntity<?> receiveData(@Validated @RequestBody Sensor data) {
		if (data.getTimestamp() == null) data.setTimestamp(LocalDateTime.now());
		Sensor saved = repo.save(data);
		return ResponseEntity.status(201).body(saved);
	}
	
	// nhận batch
	@PostMapping("/batch")
	public ResponseEntity<?> receiveBatch(@RequestBody List<Sensor> list) {
		list.forEach(d -> { if (d.getTimestamp() == null) d.setTimestamp(LocalDateTime.now()); });
		List<Sensor> saved = repo.saveAll(list);
		return ResponseEntity.status(201).body(saved);
	}

	// hiển thị danh sách record
	@GetMapping("/all")
	public List<Sensor> getAll() {
		return repo.findAll();
	}

	// tìm 1 thiết bị theo id
	@GetMapping("/device/{deviceId}")
	public List<Sensor> getByDevice(@PathVariable String deviceId) {
		return repo.findByDeviceIdOrderByTimestampDesc(deviceId);
	}
}
