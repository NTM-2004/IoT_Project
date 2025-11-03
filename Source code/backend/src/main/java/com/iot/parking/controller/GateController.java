package com.iot.parking.controller;

import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.web.bind.annotation.*;

import com.iot.parking.repository.GateRepository;

@RestController
@RequestMapping("/api/gate")
public class GateController {
	@Autowired
	GateRepository repo;
	
	@PostMapping("/open")
	public boolean openGate() {
		return true;
	}
}
