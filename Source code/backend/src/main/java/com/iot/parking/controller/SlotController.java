package com.iot.parking.controller;

import org.springframework.http.MediaType;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;

import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.servlet.mvc.method.annotation.SseEmitter;

import com.iot.parking.model.Slot;
import com.iot.parking.repository.SlotRepository;

@RestController
@RequestMapping("/api/slot")
public class SlotController {
	
	@Autowired
	private SlotRepository repo;
	private final List<SseEmitter> emitters = new CopyOnWriteArrayList<>();
	
	@GetMapping(path = "/stream", produces = MediaType.TEXT_EVENT_STREAM_VALUE)
    public SseEmitter streamEvents() {
		SseEmitter emitter = new SseEmitter(3600000L);                 
		this.emitters.add(emitter);
        
		emitter.onCompletion(() -> this.emitters.remove(emitter));
        emitter.onTimeout(() -> {
            emitter.complete(); // Đóng kết nối
            this.emitters.remove(emitter);
        });
        return emitter;
    }
	
	
	
	@PutMapping("/status")
	public boolean receiveStatus(@RequestBody Slot slot) {
		try {
			Slot temp = repo.findById(slot.getId())
					.orElseThrow(() -> new RuntimeException("Slot not found"));
			temp.setState(slot.getState());
			Slot updatedSlot = repo.save(temp);
			
			List<SseEmitter> deadEmitters = new CopyOnWriteArrayList<>();	        
	        for (SseEmitter emitter : emitters) {
	            try {
	                emitter.send(SseEmitter.event()
	                                       .name("slot-status-change") 
	                                       .data(updatedSlot, MediaType.APPLICATION_JSON));
	            } catch (IOException e) {
	                deadEmitters.add(emitter);
	            }
	        }
	        this.emitters.removeAll(deadEmitters);
			
			return true;
		}catch(Exception e) {
			e.printStackTrace();
			return false;
		}
	}
	
	@GetMapping("/show")
	public ArrayList<Slot> showSlot(){
		return (ArrayList<Slot>) repo.findAll();
	}
}
